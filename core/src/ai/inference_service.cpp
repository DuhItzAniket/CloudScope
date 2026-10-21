#include "cloudscope/ai/inference_service.hpp"

#include "cloudscope/common/log.hpp"

#include <fmt/format.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>

namespace cloudscope::ai {

namespace {

constexpr double kNightLuminance = 0.08;  // of full scale, as STRATIA's night flag (P068)

float sigmoid(float x)
{
    return 1.0F / (1.0F + std::exp(-x));
}

const hal::Tensor* find_output(const std::vector<hal::Tensor>& outputs, std::string_view name)
{
    const auto it = std::ranges::find_if(outputs, [&](const hal::Tensor& t) { return t.name == name; });
    return it == outputs.end() ? nullptr : &*it;
}

double now_ms(std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

// Height at which the conditional CDF reaches `level`, interpolated linearly inside the bin.
double height_at_cdf(const std::vector<double>& cdf, const std::vector<double>& edges, double level)
{
    const double wanted = std::clamp(level, 0.0, 1.0);
    double previous = 0.0;
    for (std::size_t k = 0; k < cdf.size(); ++k) {
        if (cdf[k] >= wanted) {
            const double span = cdf[k] - previous;
            const double t = span > 1e-12 ? (wanted - previous) / span : 1.0;
            return edges[k] + t * (edges[k + 1] - edges[k]);
        }
        previous = cdf[k];
    }
    return edges.back();
}

}  // namespace

std::vector<hal::Tensor> make_inputs(const cv::Mat& picture, const ModelCard& card, const GeometryInputs& geometry)
{
    const int h = card.input.height;
    const int w = card.input.width;
    cv::Mat resized;
    cv::resize(picture, resized, cv::Size(w, h), 0.0, 0.0, cv::INTER_AREA);
    cv::Mat rgb;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
    } else if (resized.channels() == 4) {
        cv::cvtColor(resized, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    }
    if (rgb.depth() != CV_8U) {
        rgb.convertTo(rgb, CV_8U);
    }
    hal::Tensor image{.name = std::string(kInputNames[0]), .shape = {1, 3, h, w}, .data = {}};
    image.data.resize(static_cast<std::size_t>(3) * h * w);
    const auto plane = static_cast<std::size_t>(h) * w;
    std::array<float, 3> scale{};
    std::array<float, 3> offset{};
    for (int c = 0; c < 3; ++c) {
        scale[c] = static_cast<float>(1.0 / (255.0 * card.input.std[c]));
        offset[c] = static_cast<float>(-card.input.mean[c] / card.input.std[c]);
    }
    for (int y = 0; y < h; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < w; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * w + x;
            for (int c = 0; c < 3; ++c) {
                image.data[c * plane + at] = static_cast<float>(row[x][c]) * scale[c] + offset[c];
            }
        }
    }
    const int g_h = h / card.input.patch_size;
    const int g_w = w / card.input.patch_size;
    hal::Tensor ray_map{.name = std::string(kInputNames[1]), .shape = {1, kRayMapChannels, g_h, g_w}, .data = {}};
    const std::size_t ray_count = static_cast<std::size_t>(kRayMapChannels) * g_h * g_w;
    if (geometry.valid && geometry.ray_map.size() == ray_count) {
        ray_map.data = geometry.ray_map;
    } else {
        ray_map.data.assign(ray_count, 0.0F);
    }
    hal::Tensor meta{.name = std::string(kInputNames[2]), .shape = {1, kMetaSize}, .data = {}};
    meta.data.assign(geometry.meta.begin(), geometry.meta.end());
    if (!geometry.valid) {
        std::ranges::fill(meta.data, 0.0F);
    }
    return {std::move(image), std::move(ray_map), std::move(meta)};
}

std::optional<CbhEstimate> cbh_estimate(const std::vector<float>& probs, const ModelCard& card)
{
    const int k = card.cbh_bins();
    if (static_cast<int>(probs.size()) != k + 1 || k < 1) {
        return std::nullopt;
    }
    const double p_cloud = 1.0 - probs[static_cast<std::size_t>(no_cloud_index(k))];
    if (!(p_cloud > 0.5)) {
        return std::nullopt;
    }
    std::vector<double> cdf(static_cast<std::size_t>(k));
    double running = 0.0;
    for (int i = 0; i < k; ++i) {
        running += probs[static_cast<std::size_t>(i)] / p_cloud;
        cdf[static_cast<std::size_t>(i)] = std::min(running, 1.0);
    }
    cdf.back() = 1.0;
    const auto radius = [&](const char* level) {
        const auto it = card.calibration.cbh_conformal_radius_cdf.find(level);
        return it == card.calibration.cbh_conformal_radius_cdf.end() ? 0.0 : it->second;
    };
    const double r80 = radius("80");
    const double r95 = radius("95");
    const std::vector<double>& edges = card.cbh_bin_edges_m;
    return CbhEstimate{.median_m = height_at_cdf(cdf, edges, 0.5),
                       .low_80_m = height_at_cdf(cdf, edges, 0.5 - r80),
                       .high_80_m = height_at_cdf(cdf, edges, 0.5 + r80),
                       .low_95_m = height_at_cdf(cdf, edges, 0.5 - r95),
                       .high_95_m = height_at_cdf(cdf, edges, 0.5 + r95)};
}

Expected<Prediction> decode_outputs(const std::vector<hal::Tensor>& outputs, const ModelCard& card,
                                    const cv::Mat* picture)
{
    Prediction p;
    p.model_name = card.model_name;
    p.model_version = card.model_version;
    const auto need = [&](std::string_view name, std::size_t count) -> Expected<const hal::Tensor*> {
        const hal::Tensor* t = find_output(outputs, name);
        if (t == nullptr) {
            return fail(ErrorCode::Validation, fmt::format("the model returned no '{}'", name));
        }
        if (t->data.size() != count) {
            return fail(ErrorCode::Validation,
                        fmt::format("'{}' holds {} values, {} expected", name, t->data.size(), count));
        }
        for (float v : t->data) {
            if (!std::isfinite(v)) {
                return fail(ErrorCode::Validation, fmt::format("'{}' holds a non-finite value", name));
            }
        }
        return t;
    };
    const auto genus = need(kOutputNames[0], kGenusClasses.size());
    if (!genus) {
        return fail(genus.error());
    }
    for (std::size_t i = 0; i < kGenusClasses.size(); ++i) {
        p.genus_probs[i] = sigmoid((*genus)->data[i]);
    }
    p.genus_top = static_cast<int>(std::ranges::max_element(p.genus_probs) - p.genus_probs.begin());
    const auto etage = need(kOutputNames[1], kEtageClasses.size());
    if (!etage) {
        return fail(etage.error());
    }
    for (std::size_t i = 0; i < kEtageClasses.size(); ++i) {
        p.etage_probs[i] = sigmoid((*etage)->data[i]);
    }
    const auto oktas = need(kOutputNames[2], kOktasClasses);
    if (!oktas) {
        return fail(oktas.error());
    }
    double expected = 0.0;
    for (int i = 0; i < kOktasClasses; ++i) {
        p.oktas_probs[static_cast<std::size_t>(i)] = (*oktas)->data[static_cast<std::size_t>(i)];
        expected += i * static_cast<double>(p.oktas_probs[static_cast<std::size_t>(i)]);
    }
    p.oktas = static_cast<int>(std::ranges::max_element(p.oktas_probs) - p.oktas_probs.begin());
    p.cover_fraction = std::clamp(expected / 8.0, 0.0, 1.0);

    const int dh = card.input.height / kDenseDivisor;
    const int dw = card.input.width / kDenseDivisor;
    const auto dense = static_cast<std::size_t>(dh) * dw;
    const auto sky = need(kOutputNames[3], kSkyParseClasses.size() * dense);
    if (!sky) {
        return fail(sky.error());
    }
    const auto layer = need(kOutputNames[4], kLayerClasses.size() * dense);
    if (!layer) {
        return fail(layer.error());
    }
    p.sky_parse = cv::Mat(dh, dw, CV_8UC1);
    p.layer = cv::Mat(dh, dw, CV_8UC1, cv::Scalar(255));
    std::array<std::size_t, kSkyParseClasses.size()> counts{};
    for (int y = 0; y < dh; ++y) {
        auto* sky_row = p.sky_parse.ptr<std::uint8_t>(y);
        auto* layer_row = p.layer.ptr<std::uint8_t>(y);
        for (int x = 0; x < dw; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * dw + x;
            int best = 0;
            for (std::size_t c = 1; c < kSkyParseClasses.size(); ++c) {
                if ((*sky)->data[c * dense + at] > (*sky)->data[static_cast<std::size_t>(best) * dense + at]) {
                    best = static_cast<int>(c);
                }
            }
            sky_row[x] = static_cast<std::uint8_t>(best);
            ++counts[static_cast<std::size_t>(best)];
            if (best == 2) {  // cloud: the layer is meaningful here only
                int top = 0;
                for (std::size_t c = 1; c < kLayerClasses.size(); ++c) {
                    if ((*layer)->data[c * dense + at] > (*layer)->data[static_cast<std::size_t>(top) * dense + at]) {
                        top = static_cast<int>(c);
                    }
                }
                layer_row[x] = static_cast<std::uint8_t>(top);
            }
        }
    }
    for (std::size_t c = 0; c < counts.size(); ++c) {
        p.sky_fractions[c] = static_cast<float>(counts[c]) / static_cast<float>(dense);
    }
    const auto cbh = need(kOutputNames[5], static_cast<std::size_t>(card.cbh_bins()) + 1);
    if (!cbh) {
        return fail(cbh.error());
    }
    p.cbh_probs = (*cbh)->data;
    p.p_cloud_overhead = 1.0 - p.cbh_probs.back();
    p.cbh = cbh_estimate(p.cbh_probs, card);
    const auto ood = need(kOutputNames[6], 1);
    if (!ood) {
        return fail(ood.error());
    }
    p.ood_score = (*ood)->data.front();
    if (picture != nullptr && !picture->empty()) {
        const cv::Scalar mean = cv::mean(*picture);
        const double luminance = picture->channels() >= 3 ? (mean[0] + mean[1] + mean[2]) / 3.0 : mean[0];
        p.night = luminance / 255.0 < kNightLuminance;
    }
    p.uncertain = p.night || p.ood_score > card.ood_threshold;
    return p;
}

std::vector<std::string> engine_candidates(const hal::DeviceRegistry& registry, const std::string& provider_setting)
{
    std::vector<std::string> ids;
    for (const hal::DeviceInfo& device : registry.enumerate(hal::DeviceKind::Inference)) {
        if (device.driver == "onnx") {
            ids.push_back(device.id);
        }
    }
    const auto rank = [](const std::string& id) {
        if (id == device_id(Provider::Cuda)) {
            return 0;
        }
        if (id == device_id(Provider::DirectML)) {
            return 1;
        }
        return id == device_id(Provider::Cpu) ? 2 : 3;
    };
    std::ranges::stable_sort(ids, [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
    if (provider_setting != "auto") {
        const auto wanted = provider_from_string(provider_setting);
        std::erase_if(ids, [&](const std::string& id) { return !wanted || id != device_id(*wanted); });
    }
    return ids;
}

// --------------------------------------------------------------------------------------------- the service

InferenceService::InferenceService() : worker_(std::make_unique<ThreadPool>("inference", 1)) {}

InferenceService::~InferenceService()
{
    stopping_ = true;
    worker_.reset();  // joins: a running inference finishes, nothing new starts
}

Expected<void> InferenceService::load(const std::filesystem::path& package_path, const hal::DeviceRegistry& registry,
                                      const AiSettings& settings)
{
    auto package = open_model_package(package_path);
    if (!package) {
        const std::lock_guard lock(mutex_);
        status_ = {.message = package.error().to_string()};
        return fail(package.error());
    }
    const std::vector<std::string> candidates = engine_candidates(registry, settings.provider);
    if (candidates.empty()) {
        const std::lock_guard lock(mutex_);
        status_ = {.message = onnx_runtime_available()
                                  ? fmt::format("no inference engine offers provider '{}'", settings.provider)
                                  : "this build has no ONNX Runtime"};
        return fail(ErrorCode::Unavailable, status_.message);
    }
    std::string reasons;
    for (const std::string& id : candidates) {
        auto engine = registry.create<hal::IInference>(id);
        if (!engine) {
            reasons += fmt::format("{}: {}; ", id, engine.error().message);
            continue;
        }
        if (auto opened = (*engine)->open(); !opened) {
            reasons += fmt::format("{}: {}; ", id, opened.error().message);
            continue;
        }
        auto loaded = load(*package, *engine, settings.rate_hz);
        if (loaded) {
            if (settings.provider == "auto" && id != device_id(Provider::Cpu)) {
                prefer_faster_cpu(*package, registry, settings.rate_hz);
            }
            return {};
        }
        reasons += fmt::format("{}: {}; ", id, loaded.error().message);
    }
    const std::lock_guard lock(mutex_);
    status_ = {.message = "no engine could run the model: " + reasons};
    return fail(ErrorCode::Unavailable, status_.message);
}

// "auto" follows the preference order of ADR-004 (CUDA, DirectML, CPU) but checks it: a GPU provider that runs
// the model slower than the CPU (an integrated GPU through DirectML on the development laptop: five times slower)
// is not used. One warm-up and one timed run on each; the slower engine is closed.
void InferenceService::prefer_faster_cpu(const ModelPackage& package, const hal::DeviceRegistry& registry,
                                         double rate_hz)
{
    auto cpu = registry.create<hal::IInference>(device_id(Provider::Cpu));
    if (!cpu || !(*cpu)->open() || !(*cpu)->load(package.model_file)) {
        return;
    }
    const auto capabilities = (*cpu)->capabilities();
    if (!capabilities || !check_model_io(package.card, *capabilities)) {
        return;
    }
    std::shared_ptr<hal::IInference> gpu;
    std::string gpu_label;
    {
        const std::lock_guard lock(mutex_);
        gpu = engine_;
        gpu_label = status_.provider;
    }
    const cv::Mat picture(package.card.input.height, package.card.input.width, CV_8UC3, cv::Scalar(120, 150, 200));
    const std::vector<hal::Tensor> inputs = make_inputs(picture, package.card, GeometryInputs{});
    const auto time_one = [&](hal::IInference& engine) -> double {
        if (!engine.run(inputs)) {
            return -1.0;
        }
        const auto started = std::chrono::steady_clock::now();
        if (!engine.run(inputs)) {
            return -1.0;
        }
        return now_ms(started);
    };
    const double gpu_ms = time_one(*gpu);
    const double cpu_ms = time_one(**cpu);
    if (gpu_ms < 0.0 || cpu_ms < 0.0 || gpu_ms <= cpu_ms) {
        (*cpu)->close();
        logger("ai").info("provider check: {} {:.0f} ms, CPU {:.0f} ms; keeping {}", gpu_label, gpu_ms, cpu_ms,
                          gpu_label);
        return;
    }
    logger("ai").warn("provider check: {} {:.0f} ms is slower than the CPU's {:.0f} ms; using the CPU", gpu_label,
                      gpu_ms, cpu_ms);
    if (auto switched = load(package, *cpu, rate_hz); !switched) {
        logger("ai").warn("could not switch to the CPU engine: {}", switched.error().to_string());
        return;
    }
    gpu->close();
    const std::lock_guard lock(mutex_);
    status_.message = fmt::format("{} measured {:.0f} ms per frame against the CPU's {:.0f} ms: the CPU is used",
                                  gpu_label, gpu_ms, cpu_ms);
}

Expected<void> InferenceService::load(const ModelPackage& package, std::shared_ptr<hal::IInference> engine,
                                      double rate_hz)
{
    if (!engine) {
        return fail(ErrorCode::InvalidArgument, "no engine");
    }
    if (!engine->is_open()) {
        if (auto opened = engine->open(); !opened) {
            return fail(opened.error());
        }
    }
    if (auto loaded = engine->load(package.model_file); !loaded) {
        return fail(loaded.error());
    }
    const auto capabilities = engine->capabilities();
    if (!capabilities) {
        return fail(capabilities.error());
    }
    if (auto io = check_model_io(package.card, *capabilities); !io) {
        return fail(io.error());
    }
    const std::lock_guard lock(mutex_);
    engine_ = std::move(engine);
    package_ = package;
    status_ = {.loaded = true,
               .model_name = package.card.model_name,
               .model_version = package.card.model_version,
               .provider = capabilities->provider,
               .engine_id = engine_->info().id,
               .message = {},
               .rate_hz = rate_hz};
    stats_ = {};
    pending_.reset();
    latest_.reset();
    min_interval_ = rate_hz > 0.0 ? std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                        std::chrono::duration<double>(1.0 / rate_hz))
                                  : std::chrono::steady_clock::duration::zero();
    logger("ai").info("STRATIA model {} {} ready on {} ({} per second at most)", package.card.model_name,
                      package.card.model_version, capabilities->provider, rate_hz);
    return {};
}

void InferenceService::unload()
{
    std::shared_ptr<hal::IInference> engine;
    {
        const std::lock_guard lock(mutex_);
        engine = std::move(engine_);
        package_.reset();
        pending_.reset();
        status_ = {.message = "no model loaded"};
    }
    if (engine) {
        engine->close();
    }
}

ServiceStatus InferenceService::status() const
{
    const std::lock_guard lock(mutex_);
    return status_;
}

ServiceStats InferenceService::stats() const
{
    const std::lock_guard lock(mutex_);
    return stats_;
}

std::optional<ModelCard> InferenceService::card() const
{
    const std::lock_guard lock(mutex_);
    return package_ ? std::optional<ModelCard>(package_->card) : std::nullopt;
}

std::shared_ptr<const Prediction> InferenceService::latest() const
{
    const std::lock_guard lock(mutex_);
    return latest_;
}

void InferenceService::set_callback(Callback callback)
{
    const std::lock_guard lock(mutex_);
    callback_ = std::move(callback);
}

void InferenceService::set_rate(double rate_hz)
{
    const std::lock_guard lock(mutex_);
    status_.rate_hz = rate_hz;
    min_interval_ = rate_hz > 0.0 ? std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                        std::chrono::duration<double>(1.0 / rate_hz))
                                  : std::chrono::steady_clock::duration::zero();
}

bool InferenceService::submit(std::shared_ptr<const cv::Mat> picture, const FrameInfo& info, GeometryInputs geometry)
{
    if (!picture || picture->empty()) {
        return false;
    }
    bool start = false;
    {
        const std::lock_guard lock(mutex_);
        if (!engine_ || !package_) {
            return false;
        }
        ++stats_.submitted;
        if (pending_) {
            ++stats_.dropped;  // the frame waiting is older than this one
        }
        pending_ = Pending{.picture = std::move(picture), .info = info, .geometry = std::move(geometry)};
        if (!worker_busy_) {
            worker_busy_ = true;
            start = true;
        }
    }
    if (start) {
        worker_->post([this] { worker(); });
    }
    return true;
}

void InferenceService::worker()
{
    while (!stopping_) {
        Pending job;
        std::chrono::steady_clock::duration wait{};
        {
            const std::lock_guard lock(mutex_);
            if (!pending_ || !engine_ || !package_) {
                worker_busy_ = false;
                return;
            }
            const auto since = std::chrono::steady_clock::now() - last_start_;
            wait = since < min_interval_ ? min_interval_ - since : std::chrono::steady_clock::duration::zero();
            if (wait == std::chrono::steady_clock::duration::zero()) {
                job = std::move(*pending_);
                pending_.reset();
                last_start_ = std::chrono::steady_clock::now();
            }
        }
        if (wait > std::chrono::steady_clock::duration::zero()) {
            std::this_thread::sleep_for(
                std::min(wait, std::chrono::steady_clock::duration(std::chrono::milliseconds(50))));
            continue;  // re-check: a newer frame may have replaced the waiting one
        }
        auto prediction = run_locked_engine(*job.picture, job.info, job.geometry);
        Callback callback;
        std::shared_ptr<const Prediction> result;
        {
            const std::lock_guard lock(mutex_);
            if (prediction) {
                ++stats_.run;
                stats_.last_run_ms = prediction->run_ms;
                stats_.mean_run_ms += (prediction->run_ms - stats_.mean_run_ms) / static_cast<double>(stats_.run);
                result = std::make_shared<const Prediction>(std::move(*prediction));
                latest_ = result;
                callback = callback_;
            } else {
                ++stats_.failed;
                status_.message = prediction.error().message;
            }
        }
        if (!prediction) {
            logger("ai").warn("inference failed: {}", prediction.error().to_string());
        } else if (callback) {
            callback(result);
        }
    }
    const std::lock_guard lock(mutex_);
    worker_busy_ = false;
}

Expected<Prediction> InferenceService::run_locked_engine(const cv::Mat& picture, const FrameInfo& info,
                                                         const GeometryInputs& geometry)
{
    std::shared_ptr<hal::IInference> engine;
    std::optional<ModelCard> card;
    std::string provider;
    {
        const std::lock_guard lock(mutex_);
        if (!engine_ || !package_) {
            return fail(ErrorCode::Unavailable, "no model loaded");
        }
        engine = engine_;
        card = package_->card;
        provider = status_.provider;
    }
    const auto started = std::chrono::steady_clock::now();
    const std::vector<hal::Tensor> inputs = make_inputs(picture, *card, geometry);
    const double preprocess_ms = now_ms(started);
    const auto run_started = std::chrono::steady_clock::now();
    auto outputs = engine->run(inputs);  // the engine serialises its own runs
    if (!outputs) {
        return fail(outputs.error());
    }
    const double run_ms = now_ms(run_started);
    auto prediction = decode_outputs(*outputs, *card, &picture);
    if (!prediction) {
        return fail(prediction.error());
    }
    prediction->frame = info;
    prediction->provider = provider;
    prediction->geometry_valid = geometry.valid;
    prediction->preprocess_ms = preprocess_ms;
    prediction->run_ms = run_ms;
    prediction->total_ms = now_ms(started);
    return prediction;
}

Expected<Prediction> InferenceService::run_now(const cv::Mat& picture, const FrameInfo& info,
                                               const GeometryInputs& geometry)
{
    return run_locked_engine(picture, info, geometry);
}

}  // namespace cloudscope::ai
