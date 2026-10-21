// The inference service (P066, FR-AI-02, FR-AI-03): loads a model package on the best available engine, runs it
// on its own thread with latest-frame-only semantics at a bounded rate, and turns the contract's seven output
// tensors into a `Prediction` a host can show and log.
//
// Threading (architecture §4.1): `submit()` never blocks and never queues; a frame that arrives while the
// worker is busy replaces the one waiting, which is counted as dropped. The callback runs on the worker thread;
// hosts marshal it to their own thread (the desktop uses QtExecutor).

#pragma once

#include <cloudscope/ai/contract.hpp>
#include <cloudscope/ai/model_card.hpp>
#include <cloudscope/ai/onnx_engine.hpp>
#include <cloudscope/ai/settings.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/common/executor.hpp>
#include <cloudscope/hal/inference.hpp>
#include <cloudscope/hal/registry.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cloudscope::ai {

// The model's geometry inputs for one frame (P067 computes them; all zeros = unknown, which the model accepts).
struct GeometryInputs {
    std::vector<float> ray_map;  // 4 x (H/p) x (W/p), row-major; empty or zeros when unknown
    std::array<float, 3> meta{0.0F, 0.0F, 0.0F};
    bool valid = false;  // true when the ray map and meta come from a calibration, a pose and the Sun
};

struct CbhEstimate {
    double median_m = 0.0;
    double low_80_m = 0.0;
    double high_80_m = 0.0;
    double low_95_m = 0.0;
    double high_95_m = 0.0;
};

struct Prediction {
    FrameInfo frame;  // the frame the result belongs to
    std::string model_name;
    std::string model_version;
    std::string provider;  // what executed the model
    bool geometry_valid = false;

    std::array<float, kGenusClasses.size()> genus_probs{};  // sigmoid of the (calibrated) logits
    int genus_top = 0;                                      // index into kGenusClasses
    std::array<float, kEtageClasses.size()> etage_probs{};
    std::array<float, kOktasClasses> oktas_probs{};
    int oktas = 0;                // argmax
    double cover_fraction = 0.0;  // expected oktas / 8

    std::vector<float> cbh_probs;  // k + 1 (last = no cloud overhead)
    double p_cloud_overhead = 0.0;
    std::optional<CbhEstimate> cbh;  // when p_cloud_overhead > 0.5

    cv::Mat sky_parse;  // CV_8UC1 class index per pixel at H/4 x W/4 (kSkyParseClasses)
    cv::Mat layer;      // CV_8UC1 layer index (kLayerClasses) on cloud pixels, 255 elsewhere
    std::array<float, kSkyParseClasses.size()> sky_fractions{};

    float ood_score = 0.0F;
    bool uncertain = false;  // ood_score above the card's threshold, or a night frame (FR-AI-06)
    bool night = false;      // mean luminance below 8 % of full scale

    double preprocess_ms = 0.0;
    double run_ms = 0.0;
    double total_ms = 0.0;
};

struct ServiceStatus {
    bool loaded = false;
    std::string model_name;
    std::string model_version;
    std::string provider;   // of the engine in use
    std::string engine_id;  // device id
    std::string message;    // why nothing is loaded, or a note
    double rate_hz = 0.0;
};

struct ServiceStats {
    std::uint64_t submitted = 0;
    std::uint64_t run = 0;
    std::uint64_t dropped = 0;  // replaced while waiting (FR-AI-03)
    std::uint64_t failed = 0;
    double mean_run_ms = 0.0;
    double last_run_ms = 0.0;
};

// Contract inputs for one picture (BGR or grey 8-bit), normalised as the card says.
[[nodiscard]] std::vector<hal::Tensor> make_inputs(const cv::Mat& picture, const ModelCard& card,
                                                   const GeometryInputs& geometry);

// The seven outputs of one run, decoded. `night` is computed from `picture` when given.
[[nodiscard]] Expected<Prediction> decode_outputs(const std::vector<hal::Tensor>& outputs, const ModelCard& card,
                                                  const cv::Mat* picture = nullptr);

// Median and intervals of the cloud-base distribution (conformal radii from the card, CDF units).
[[nodiscard]] std::optional<CbhEstimate> cbh_estimate(const std::vector<float>& probs, const ModelCard& card);

// The engines a service may use, most preferred first, honouring the setting ("auto" or one provider).
[[nodiscard]] std::vector<std::string> engine_candidates(const hal::DeviceRegistry& registry,
                                                         const std::string& provider_setting);

class InferenceService {
public:
    using Callback = std::function<void(std::shared_ptr<const Prediction>)>;

    InferenceService();
    ~InferenceService();
    InferenceService(const InferenceService&) = delete;
    InferenceService& operator=(const InferenceService&) = delete;
    InferenceService(InferenceService&&) = delete;
    InferenceService& operator=(InferenceService&&) = delete;

    // Opens the package, picks the first candidate engine that loads it and declares the contract's tensors,
    // and reports what happened. Replaces a previous model; on failure nothing stays loaded.
    [[nodiscard]] Expected<void> load(const std::filesystem::path& package, const hal::DeviceRegistry& registry,
                                      const AiSettings& settings);
    // Like load(), with an engine the caller made (tests, the benchmark).
    [[nodiscard]] Expected<void> load(const ModelPackage& package, std::shared_ptr<hal::IInference> engine,
                                      double rate_hz);
    void unload();

    [[nodiscard]] ServiceStatus status() const;
    [[nodiscard]] ServiceStats stats() const;
    [[nodiscard]] std::optional<ModelCard> card() const;
    [[nodiscard]] std::shared_ptr<const Prediction> latest() const;

    void set_callback(Callback callback);  // called on the worker thread for every completed run
    void set_rate(double rate_hz);

    // Hands over the newest frame. Never blocks. Returns false when nothing is loaded.
    bool submit(std::shared_ptr<const cv::Mat> picture, const FrameInfo& info, GeometryInputs geometry);
    // Runs one picture now, on the calling thread (the benchmark, tests).
    [[nodiscard]] Expected<Prediction> run_now(const cv::Mat& picture, const FrameInfo& info,
                                               const GeometryInputs& geometry);

private:
    struct Pending {
        std::shared_ptr<const cv::Mat> picture;
        FrameInfo info;
        GeometryInputs geometry;
    };
    void worker();
    void prefer_faster_cpu(const ModelPackage& package, const hal::DeviceRegistry& registry, double rate_hz);
    [[nodiscard]] Expected<Prediction> run_locked_engine(const cv::Mat& picture, const FrameInfo& info,
                                                         const GeometryInputs& geometry);

    mutable std::mutex mutex_;
    std::shared_ptr<hal::IInference> engine_;
    std::optional<ModelPackage> package_;
    ServiceStatus status_;
    ServiceStats stats_;
    Callback callback_;
    std::optional<Pending> pending_;
    bool worker_busy_ = false;
    std::chrono::steady_clock::time_point last_start_{};
    std::chrono::steady_clock::duration min_interval_{};
    std::shared_ptr<const Prediction> latest_;
    std::atomic<bool> stopping_{false};
    std::unique_ptr<ThreadPool> worker_;  // one thread; last member so it is joined first
};

}  // namespace cloudscope::ai
