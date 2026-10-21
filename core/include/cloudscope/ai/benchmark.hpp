// Inference benchmark per engine and execution provider (P070, FR-AI-09, NFR-PERF-05).

#pragma once

#include <cloudscope/ai/model_card.hpp>
#include <cloudscope/common/error.hpp>
#include <cloudscope/hal/registry.hpp>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace cloudscope::ai {

struct BenchmarkOptions {
    int runs = 20;                 // timed runs per engine
    int warmup = 3;                // untimed runs before them
    std::string provider = "all";  // "all", "auto" (the first that works) or one provider name
    int width = 1920;              // of the synthetic picture when none is given
    int height = 1080;
};

struct BenchmarkRow {
    std::string device_id;
    std::string provider;
    bool ok = false;
    std::string error;           // why the engine could not run the model
    double load_ms = 0.0;        // session creation
    double preprocess_ms = 0.0;  // resize and normalisation of the picture (host work, once)
    double mean_ms = 0.0;        // engine run only
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    int runs = 0;
};

struct BenchmarkReport {
    std::string model_name;
    std::string model_version;
    std::string onnx_runtime;
    std::string machine;  // host name and operating system
    int width = 0;        // of the picture that was fed
    int height = 0;
    std::vector<BenchmarkRow> rows;
};

// Runs the model on every candidate engine of the registry. `picture` (8-bit BGR or grey) replaces the synthetic
// sky. Engines that cannot load the model get a row with `ok = false` and the reason; the report fails only when
// the package itself is unusable or no engine exists.
[[nodiscard]] Expected<BenchmarkReport> benchmark_model(const ModelPackage& package,
                                                        const hal::DeviceRegistry& registry,
                                                        const BenchmarkOptions& options,
                                                        const cv::Mat* picture = nullptr);

[[nodiscard]] std::string benchmark_markdown(const BenchmarkReport& report);
[[nodiscard]] nlohmann::json benchmark_json(const BenchmarkReport& report);

}  // namespace cloudscope::ai
