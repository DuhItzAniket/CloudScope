#include "cloudscope/ai/benchmark.hpp"

#include "cloudscope/ai/inference_service.hpp"
#include "cloudscope/ai/onnx_engine.hpp"
#include "cloudscope/sim/synthetic_sky.hpp"

#include <QtCore/QSysInfo>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace cloudscope::ai {

namespace {

double ms_since(std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

// Linear interpolation between the neighbouring order statistics of an ascending vector.
double percentile(const std::vector<double>& sorted, double fraction)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const auto low = static_cast<std::size_t>(std::floor(position));
    const auto high = std::min(low + 1, sorted.size() - 1);
    const double t = position - static_cast<double>(low);
    return sorted[low] + t * (sorted[high] - sorted[low]);
}

}  // namespace

Expected<BenchmarkReport> benchmark_model(const ModelPackage& package, const hal::DeviceRegistry& registry,
                                          const BenchmarkOptions& options, const cv::Mat* picture)
{
    BenchmarkReport report;
    report.model_name = package.card.model_name;
    report.model_version = package.card.model_version;
    report.onnx_runtime = onnx_runtime_version();
    report.machine =
        fmt::format("{} ({})", QSysInfo::machineHostName().toStdString(), QSysInfo::prettyProductName().toStdString());
    cv::Mat frame;
    if (picture != nullptr && !picture->empty()) {
        frame = *picture;
    } else {
        sim::SyntheticSkySpec spec;
        spec.width = options.width;
        spec.height = options.height;
        auto sky = sim::make_synthetic_sky(spec);
        if (!sky) {
            return fail(sky.error());
        }
        frame = sky->image;
    }
    report.width = frame.cols;
    report.height = frame.rows;
    const std::vector<std::string> candidates =
        engine_candidates(registry, options.provider == "all" ? "auto" : options.provider);
    if (candidates.empty()) {
        return fail(ErrorCode::Unavailable, fmt::format("no inference engine offers provider '{}'", options.provider));
    }
    const auto started_preprocess = std::chrono::steady_clock::now();
    const std::vector<hal::Tensor> inputs = make_inputs(frame, package.card, GeometryInputs{});
    const double preprocess_ms = ms_since(started_preprocess);
    for (const std::string& id : candidates) {
        BenchmarkRow row;
        row.device_id = id;
        row.preprocess_ms = preprocess_ms;
        auto engine = registry.create<hal::IInference>(id);
        if (!engine) {
            row.error = engine.error().message;
            report.rows.push_back(row);
            continue;
        }
        if (auto opened = (*engine)->open(); !opened) {
            row.error = opened.error().message;
            report.rows.push_back(row);
            continue;
        }
        const auto started_load = std::chrono::steady_clock::now();
        if (auto loaded = (*engine)->load(package.model_file); !loaded) {
            row.error = loaded.error().message;
            report.rows.push_back(row);
            continue;
        }
        row.load_ms = ms_since(started_load);
        const auto capabilities = (*engine)->capabilities();
        if (!capabilities) {
            row.error = capabilities.error().message;
            report.rows.push_back(row);
            continue;
        }
        row.provider = capabilities->provider;
        if (auto io = check_model_io(package.card, *capabilities); !io) {
            row.error = io.error().message;
            report.rows.push_back(row);
            continue;
        }
        bool failed = false;
        for (int i = 0; i < options.warmup && !failed; ++i) {
            if (auto out = (*engine)->run(inputs); !out) {
                row.error = out.error().message;
                failed = true;
            }
        }
        std::vector<double> times;
        for (int i = 0; i < options.runs && !failed; ++i) {
            const auto started = std::chrono::steady_clock::now();
            auto out = (*engine)->run(inputs);
            if (!out) {
                row.error = out.error().message;
                failed = true;
                break;
            }
            times.push_back(ms_since(started));
        }
        if (!failed && !times.empty()) {
            // Sorted in place: a copy of the vector here is flagged by GCC 14's -Wnull-dereference at -O3.
            std::ranges::sort(times);
            row.ok = true;
            row.runs = static_cast<int>(times.size());
            row.mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size());
            row.p50_ms = percentile(times, 0.5);
            row.p95_ms = percentile(times, 0.95);
            row.min_ms = times.front();
            row.max_ms = times.back();
        }
        (*engine)->close();
        report.rows.push_back(row);
        if (options.provider == "auto" && row.ok) {
            break;
        }
    }
    return report;
}

std::string benchmark_markdown(const BenchmarkReport& report)
{
    std::string out = fmt::format(
        "# STRATIA inference benchmark\n\nModel `{}` {}; ONNX Runtime {}; {}; picture {} x {} "
        "(resized to the model's input by the host).\n\n",
        report.model_name, report.model_version, report.onnx_runtime, report.machine, report.width, report.height);
    out += "| Engine | Provider | Load (ms) | Preprocess (ms) | Mean (ms) | p50 (ms) | p95 (ms) | Min (ms) | Max (ms) "
           "| Runs | Note |\n";
    out += "|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const BenchmarkRow& row : report.rows) {
        if (row.ok) {
            out += fmt::format("| `{}` | {} | {:.0f} | {:.1f} | {:.1f} | {:.1f} | {:.1f} | {:.1f} | {:.1f} | {} | |\n",
                               row.device_id, row.provider, row.load_ms, row.preprocess_ms, row.mean_ms, row.p50_ms,
                               row.p95_ms, row.min_ms, row.max_ms, row.runs);
        } else {
            out += fmt::format("| `{}` | {} | – | – | – | – | – | – | – | 0 | {} |\n", row.device_id,
                               row.provider.empty() ? "–" : row.provider, row.error);
        }
    }
    return out;
}

nlohmann::json benchmark_json(const BenchmarkReport& report)
{
    nlohmann::json rows = nlohmann::json::array();
    for (const BenchmarkRow& row : report.rows) {
        rows.push_back({{"device_id", row.device_id},
                        {"provider", row.provider},
                        {"ok", row.ok},
                        {"error", row.error},
                        {"load_ms", row.load_ms},
                        {"preprocess_ms", row.preprocess_ms},
                        {"mean_ms", row.mean_ms},
                        {"p50_ms", row.p50_ms},
                        {"p95_ms", row.p95_ms},
                        {"min_ms", row.min_ms},
                        {"max_ms", row.max_ms},
                        {"runs", row.runs}});
    }
    return {{"model_name", report.model_name},
            {"model_version", report.model_version},
            {"onnx_runtime", report.onnx_runtime},
            {"machine", report.machine},
            {"width", report.width},
            {"height", report.height},
            {"rows", rows}};
}

}  // namespace cloudscope::ai
