// P066: the ONNX Runtime engine obeys the inference contract on STRATIA's dummy model, matches the Python
// reference outputs, and the inference service runs the model off the caller's thread without queuing.

#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/ai/contract.hpp>
#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/ai/model_card.hpp>
#include <cloudscope/ai/onnx_engine.hpp>
#include <cloudscope/ai/settings.hpp>
#include <cloudscope/common/app_config.hpp>
#include <cloudscope/hal/registry.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

using namespace cloudscope;
using namespace cloudscope::ai;
using Catch::Matchers::WithinAbs;
using cloudscope::test::check_inference_contract;
using cloudscope::test::data_path;
using cloudscope::test::outcome;

namespace {

constexpr double kReferenceTolerance = 1e-4;

ModelPackage dummy_package()
{
    auto package = open_model_package(cloudscope::test::data_dir() / "stratia");
    REQUIRE(outcome(package) == "ok");
    return *package;
}

std::shared_ptr<OnnxInference> cpu_engine()
{
    return OnnxInference::make({.provider = Provider::Cpu, .threads = 2, .device_id = 0});
}

// The closed-form inputs of STRATIA's scripts/make_contract_reference.py.
std::vector<hal::Tensor> reference_inputs(int size = 512, int patch = 16)
{
    hal::Tensor image{.name = "image", .shape = {1, 3, size, size}, .data = {}};
    image.data.resize(static_cast<std::size_t>(3) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int v = (7 * x + 13 * y + 29 * c) % 251;
                image
                    .data[(static_cast<std::size_t>(c) * static_cast<std::size_t>(size) + static_cast<std::size_t>(y)) *
                              static_cast<std::size_t>(size) +
                          static_cast<std::size_t>(x)] = static_cast<float>(v / 250.0 * 2.0 - 1.0);
            }
        }
    }
    const int g = size / patch;
    hal::Tensor rays{.name = "ray_map", .shape = {1, 4, g, g}, .data = {}};
    rays.data.assign(static_cast<std::size_t>(4) * static_cast<std::size_t>(g) * static_cast<std::size_t>(g), 0.0F);
    for (int r = 0; r < g; ++r) {
        for (int col = 0; col < g; ++col) {
            if ((r + col) % 7 == 0) {
                continue;
            }
            const double dx = (col + 0.5) / g - 0.5;
            const double dy = (r + 0.5) / g - 0.5;
            const double dz = 0.8;
            const double norm = std::sqrt(dx * dx + dy * dy + dz * dz);
            const std::size_t at =
                static_cast<std::size_t>(r) * static_cast<std::size_t>(g) + static_cast<std::size_t>(col);
            const std::size_t plane = static_cast<std::size_t>(g) * static_cast<std::size_t>(g);
            rays.data[at] = static_cast<float>(dx / norm);
            rays.data[plane + at] = static_cast<float>(dy / norm);
            rays.data[2 * plane + at] = static_cast<float>(dz / norm);
            rays.data[3 * plane + at] = 1.0F;
        }
    }
    hal::Tensor meta{.name = "meta", .shape = {1, 3}, .data = {0.6F, 0.8F, 1.0F}};
    return {std::move(image), std::move(rays), std::move(meta)};
}

const hal::Tensor& output(const std::vector<hal::Tensor>& outputs, std::string_view name)
{
    const auto it = std::ranges::find_if(outputs, [&](const hal::Tensor& t) { return t.name == name; });
    REQUIRE(it != outputs.end());
    return *it;
}

}  // namespace

TEST_CASE("the ONNX Runtime engine obeys the inference contract on the dummy model", "[ai][onnx][contract]")
{
    if (!onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    check_inference_contract([] { return cpu_engine(); }, data_path("stratia/stratia_dummy.onnx"));
}

TEST_CASE("the driver offers one device per available provider, the CPU last", "[ai][onnx]")
{
    const auto driver = OnnxDriver::make();
    const std::vector<hal::DeviceInfo> devices = driver->enumerate();
    if (!onnx_runtime_available()) {
        CHECK(devices.empty());
        return;
    }
    REQUIRE_FALSE(devices.empty());
    CHECK(devices.back().id == "onnx:inference:cpu");
    for (const hal::DeviceInfo& d : devices) {
        CHECK(d.kind == hal::DeviceKind::Inference);
        CHECK(d.driver == "onnx");
        CHECK_FALSE(d.simulated);
        CHECK(outcome(driver->create(d.id)) == "ok");
    }
    CHECK(outcome(driver->create("onnx:inference:abacus")) == "NotFound");
    CHECK(provider_from_string("DirectML") == Provider::DirectML);
    CHECK(provider_from_string("CPU") == Provider::Cpu);
    CHECK_FALSE(provider_from_string("tpu").has_value());
    CHECK(device_id(Provider::Cuda) == "onnx:inference:cuda");
    CHECK_FALSE(onnx_runtime_version().empty());
}

TEST_CASE("the dummy model declares the contract's tensors and matches the Python reference", "[ai][onnx]")
{
    if (!onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    const ModelPackage package = dummy_package();
    const auto engine = cpu_engine();
    REQUIRE(outcome(engine->open()) == "ok");
    REQUIRE(outcome(engine->load(package.model_file)) == "ok");
    const auto capabilities = engine->capabilities();
    REQUIRE(outcome(capabilities) == "ok");
    CHECK(capabilities->provider == "CPU");
    CHECK(outcome(check_model_io(package.card, *capabilities)) == "ok");

    // the card travels inside the ONNX file too
    const auto embedded = engine->metadata("stratia_model_card");
    REQUIRE(outcome(embedded) == "ok");
    REQUIRE(embedded->has_value());
    const auto card_in_graph = model_card_from_json(nlohmann::json::parse(**embedded));
    REQUIRE(outcome(card_in_graph) == "ok");
    CHECK(card_in_graph->model_name == package.card.model_name);
    CHECK_FALSE(engine->metadata("no-such-key")->has_value());

    const auto outputs = engine->run(reference_inputs(package.card.input.height, package.card.input.patch_size));
    REQUIRE(outcome(outputs) == "ok");
    std::ifstream in(data_path("stratia/reference.json"));
    REQUIRE(in);
    const nlohmann::json reference = nlohmann::json::parse(in);
    for (const std::string_view name : {"genus_logits", "etage_logits", "oktas_probs", "cbh_probs", "ood_score"}) {
        const hal::Tensor& got = output(*outputs, name);
        const nlohmann::json& want = reference.at("outputs").at(std::string(name));
        REQUIRE(got.data.size() == want.size());
        for (std::size_t i = 0; i < got.data.size(); ++i) {
            INFO(name << "[" << i << "]");
            CHECK_THAT(got.data[i], WithinAbs(want[i].get<double>(), kReferenceTolerance));
        }
    }
    for (const std::string_view name : {"sky_parse_logits", "layer_logits"}) {
        const hal::Tensor& got = output(*outputs, name);
        const nlohmann::json& want = reference.at("outputs").at(std::string(name));
        const auto channels = static_cast<std::size_t>(got.shape[1]);
        const auto h = static_cast<std::size_t>(got.shape[2]);
        const auto w = static_cast<std::size_t>(got.shape[3]);
        REQUIRE(want.at("shape") == nlohmann::json::array({channels, h, w}));
        for (std::size_t c = 0; c < channels; ++c) {
            double sum = 0.0;
            for (std::size_t i = 0; i < h * w; ++i) {
                sum += static_cast<double>(got.data[c * h * w + i]);
            }
            INFO(name << " channel " << c << " mean");
            CHECK_THAT(sum / static_cast<double>(h * w),
                       WithinAbs(want.at("channel_mean")[c].get<double>(), kReferenceTolerance));
        }
        std::size_t k = 0;
        for (const auto& row : want.at("sample_rows")) {
            for (const auto& col : want.at("sample_cols")) {
                const auto r = row.get<std::size_t>();
                const auto cc = col.get<std::size_t>();
                for (std::size_t c = 0; c < channels; ++c) {
                    INFO(name << " sample (" << r << ", " << cc << ") channel " << c);
                    CHECK_THAT(got.data[c * h * w + r * w + cc],
                               WithinAbs(want.at("samples")[k][c].get<double>(), kReferenceTolerance));
                }
                ++k;
            }
        }
    }
}

TEST_CASE("inputs are built as the card says and outputs decode into a prediction", "[ai][service]")
{
    const ModelPackage package = dummy_package();
    cv::Mat picture(300, 400, CV_8UC3, cv::Scalar(30, 60, 200));  // BGR: a reddish picture
    const std::vector<hal::Tensor> inputs = make_inputs(picture, package.card, GeometryInputs{});
    REQUIRE(inputs.size() == 3);
    CHECK(inputs[0].shape == std::vector<std::int64_t>{1, 3, 512, 512});
    CHECK(inputs[1].shape == std::vector<std::int64_t>{1, 4, 32, 32});
    CHECK(inputs[2].data == std::vector<float>{0.0F, 0.0F, 0.0F});
    // channel 0 of the input is red (BGR -> RGB), normalised with the card's mean and std
    const double red = (200.0 / 255.0 - package.card.input.mean[0]) / package.card.input.std[0];
    CHECK_THAT(inputs[0].data[0], WithinAbs(red, 1e-5));
    const double blue = (30.0 / 255.0 - package.card.input.mean[2]) / package.card.input.std[2];
    CHECK_THAT(inputs[0].data[std::size_t{2} * 512 * 512], WithinAbs(blue, 1e-5));
    for (float v : inputs[1].data) {
        CHECK(v == 0.0F);  // no geometry: an all-zero ray map
    }

    GeometryInputs geometry;
    geometry.valid = true;
    geometry.ray_map.assign(std::size_t{4} * 32 * 32, 0.5F);
    geometry.meta = {0.6F, 0.8F, 1.0F};
    const std::vector<hal::Tensor> with_geometry = make_inputs(picture, package.card, geometry);
    CHECK(with_geometry[1].data[7] == 0.5F);
    CHECK(with_geometry[2].data == std::vector<float>{0.6F, 0.8F, 1.0F});

    // decoding: hand-made outputs with a known structure
    std::vector<hal::Tensor> outputs;
    outputs.push_back({.name = "genus_logits", .shape = {1, 12}, .data = std::vector<float>(12, -2.0F)});
    outputs.back().data[8] = 3.0F;  // Cu
    outputs.push_back({.name = "etage_logits", .shape = {1, 3}, .data = {4.0F, -4.0F, -4.0F}});
    std::vector<float> oktas(9, 0.0F);
    oktas[3] = 0.6F;
    oktas[4] = 0.4F;
    outputs.push_back({.name = "oktas_probs", .shape = {1, 9}, .data = oktas});
    const std::size_t dense = std::size_t{128} * 128;
    std::vector<float> sky(4 * dense, 0.0F);
    for (std::size_t i = 0; i < dense; ++i) {
        sky[(i < dense / 2 ? 1 : 2) * dense + i] = 5.0F;  // top half sky, bottom half cloud
    }
    outputs.push_back({.name = "sky_parse_logits", .shape = {1, 4, 128, 128}, .data = sky});
    std::vector<float> layer(3 * dense, 0.0F);
    for (std::size_t i = 0; i < dense; ++i) {
        layer[dense + i] = 1.0F;  // mid everywhere
    }
    outputs.push_back({.name = "layer_logits", .shape = {1, 3, 128, 128}, .data = layer});
    std::vector<float> cbh(25, 0.0F);
    cbh[4] = 0.5F;  // bins 4 and 5 of the low étage
    cbh[5] = 0.3F;
    cbh[24] = 0.2F;  // no cloud overhead
    outputs.push_back({.name = "cbh_probs", .shape = {1, 25}, .data = cbh});
    outputs.push_back({.name = "ood_score", .shape = {1, 1}, .data = {0.5F}});

    const auto prediction = decode_outputs(outputs, package.card, &picture);
    REQUIRE(outcome(prediction) == "ok");
    CHECK(prediction->genus_top == 8);
    CHECK(prediction->genus_probs[8] > 0.9F);
    CHECK(prediction->etage_probs[0] > 0.9F);
    CHECK(prediction->oktas == 3);
    CHECK_THAT(prediction->cover_fraction, WithinAbs(3.4 / 8.0, 1e-6));
    CHECK_THAT(prediction->sky_fractions[1], WithinAbs(0.5, 1e-6));
    CHECK_THAT(prediction->sky_fractions[2], WithinAbs(0.5, 1e-6));
    CHECK(prediction->sky_parse.at<std::uint8_t>(0, 0) == 1);
    CHECK(prediction->sky_parse.at<std::uint8_t>(127, 0) == 2);
    CHECK(prediction->layer.at<std::uint8_t>(0, 0) == 255);  // sky: no layer
    CHECK(prediction->layer.at<std::uint8_t>(127, 0) == 1);  // cloud: mid
    CHECK_THAT(prediction->p_cloud_overhead, WithinAbs(0.8, 1e-6));
    REQUIRE(prediction->cbh.has_value());
    const double lo4 = package.card.cbh_bin_edges_m[4];
    const double hi5 = package.card.cbh_bin_edges_m[6];
    CHECK(prediction->cbh->median_m > lo4);
    CHECK(prediction->cbh->median_m < hi5);
    CHECK(prediction->cbh->low_80_m <= prediction->cbh->median_m);
    CHECK(prediction->cbh->high_80_m >= prediction->cbh->median_m);
    CHECK(prediction->cbh->low_95_m <= prediction->cbh->low_80_m);
    CHECK(prediction->cbh->high_95_m >= prediction->cbh->high_80_m);
    CHECK_FALSE(prediction->night);
    CHECK(prediction->uncertain == (0.5 > package.card.ood_threshold));

    outputs.back().data = {static_cast<float>(package.card.ood_threshold + 1.0)};
    CHECK(decode_outputs(outputs, package.card, &picture)->uncertain);
    const cv::Mat dark(300, 400, CV_8UC3, cv::Scalar(5, 5, 5));
    const auto at_night = decode_outputs(outputs, package.card, &dark);
    CHECK(at_night->night);
    CHECK(at_night->uncertain);

    // no cloud overhead: no height
    std::vector<float> clear(25, 0.0F);
    clear[24] = 0.9F;
    clear[0] = 0.1F;
    outputs[5].data = clear;
    CHECK_FALSE(decode_outputs(outputs, package.card, &picture)->cbh.has_value());

    outputs[0].data[3] = std::numeric_limits<float>::quiet_NaN();
    CHECK(outcome(decode_outputs(outputs, package.card, &picture)) == "Validation");
    outputs.pop_back();
    CHECK(outcome(decode_outputs(outputs, package.card, &picture)) == "Validation");
}

TEST_CASE("the service runs the dummy model on the best engine and never queues frames", "[ai][service]")
{
    if (!onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    hal::DeviceRegistry registry;
    REQUIRE(outcome(registry.add_driver(OnnxDriver::make())) == "ok");
    AiSettings settings;
    settings.provider = "cpu";
    settings.rate_hz = 50.0;
    InferenceService service;
    CHECK_FALSE(service.status().loaded);

    const auto missing = service.load(cloudscope::test::data_dir() / "nowhere", registry, settings);
    CHECK(outcome(missing) == "NotFound");
    CHECK_FALSE(service.status().loaded);

    REQUIRE(outcome(service.load(cloudscope::test::data_dir() / "stratia", registry, settings)) == "ok");
    const ServiceStatus status = service.status();
    CHECK(status.loaded);
    CHECK(status.model_name == "stratia-dummy");
    CHECK(status.provider == "CPU");
    CHECK(status.engine_id == "onnx:inference:cpu");
    REQUIRE(service.card().has_value());

    std::mutex mutex;
    std::condition_variable ready;
    std::vector<std::uint64_t> sequences;
    service.set_callback([&](const std::shared_ptr<const Prediction>& p) {
        const std::lock_guard lock(mutex);
        sequences.push_back(p->frame.sequence);
        ready.notify_all();
    });

    const auto picture = std::make_shared<const cv::Mat>(240, 320, CV_8UC3, cv::Scalar(120, 160, 200));
    FrameInfo info;
    info.width = 320;
    info.height = 240;
    for (std::uint64_t i = 0; i < 20; ++i) {
        info.sequence = i;
        CHECK(service.submit(picture, info, GeometryInputs{}));
    }
    {
        std::unique_lock lock(mutex);
        REQUIRE(ready.wait_for(lock, std::chrono::seconds(30),
                               [&] { return !sequences.empty() && sequences.back() == 19; }));
    }
    // give the worker a moment to go idle, then look at the counts
    for (int i = 0; i < 100 && service.stats().run + service.stats().dropped + service.stats().failed < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const ServiceStats stats = service.stats();
    CHECK(stats.submitted == 20);
    CHECK(stats.failed == 0);
    CHECK(stats.run >= 1);
    CHECK(stats.run + stats.dropped == 20);  // every frame was either run or replaced, none queued
    CHECK(stats.dropped >= 1);               // 20 frames in a burst cannot all run
    CHECK(sequences.back() == 19);           // the newest frame is always the one that runs last
    REQUIRE(service.latest() != nullptr);
    CHECK(service.latest()->frame.sequence == 19);
    CHECK(service.latest()->provider == "CPU");
    CHECK(service.latest()->run_ms > 0.0);

    // a direct run gives the same kind of result on the calling thread
    const auto now = service.run_now(*picture, info, GeometryInputs{});
    REQUIRE(outcome(now) == "ok");
    CHECK(now->model_version == service.card()->model_version);
    CHECK(now->cbh_probs.size() == 25);

    service.unload();
    CHECK_FALSE(service.status().loaded);
    CHECK_FALSE(service.submit(picture, info, GeometryInputs{}));
}

TEST_CASE("the rate limit spaces inferences out", "[ai][service]")
{
    if (!onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    InferenceService service;
    REQUIRE(outcome(service.load(dummy_package(), cpu_engine(), 5.0)) == "ok");  // at most one run per 200 ms
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> times;
    service.set_callback([&](const std::shared_ptr<const Prediction>&) {
        const std::lock_guard lock(mutex);
        times.push_back(std::chrono::steady_clock::now());
    });
    const auto picture = std::make_shared<const cv::Mat>(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
    FrameInfo info;
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::milliseconds(650)) {
        ++info.sequence;
        service.submit(picture, info, GeometryInputs{});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const std::lock_guard lock(mutex);
    REQUIRE(times.size() >= 2);
    CHECK(times.size() <= 6);  // ~650 ms at 5 per second, plus the frame left waiting at the end
    for (std::size_t i = 1; i < times.size(); ++i) {
        CHECK(times[i] - times[i - 1] >= std::chrono::milliseconds(150));
    }
}

TEST_CASE("the [ai] configuration section is read and the engines are configured drivers", "[ai][config]")
{
    const auto effective = app_config_format().defaults();
    const auto settings = ai_settings(effective);
    REQUIRE(outcome(settings) == "ok");
    CHECK(settings->enabled);
    CHECK(settings->model_dir.empty());
    CHECK(settings->provider == "auto");
    CHECK_THAT(settings->rate_hz, WithinAbs(2.0, 1e-9));
    CHECK(settings->threads == 0);
    CHECK(outcome(ai_settings(nlohmann::json::object())) == "InvalidArgument");

    hal::DeviceRegistry registry;
    REQUIRE(outcome(registry.add_driver(OnnxDriver::make())) == "ok");
    const std::vector<std::string> automatic = engine_candidates(registry, "auto");
    if (onnx_runtime_available()) {
        REQUIRE_FALSE(automatic.empty());
        CHECK(automatic.back() == "onnx:inference:cpu");
        CHECK(engine_candidates(registry, "cpu") == std::vector<std::string>{"onnx:inference:cpu"});
        CHECK(engine_candidates(registry, "abacus").empty());
    } else {
        CHECK(automatic.empty());
    }
}
