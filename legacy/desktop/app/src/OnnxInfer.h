#pragma once
#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

// ONNX Runtime inference: 11-class classifier (224) + 3-class segmenter (512).
// CUDA EP first, CPU fallback. Manual NCHW preprocess == Python pipeline.
class OnnxInfer {
public:
    struct ClassResult {
        int id = -1;
        std::string label;
        float confidence = 0.0f;
        std::vector<std::pair<std::string, float>> top3;
    };

    OnnxInfer();

    // Returns true if at least one model loaded.
    bool load(const std::string& classifierPath, const std::string& segPath);
    bool hasClassifier() const { return clsSession_ != nullptr; }
    bool hasSegmenter() const { return segSession_ != nullptr; }
    // "cuda" when any session runs on CUDA, else "cpu", else "unloaded".
    std::string backend() const { return backend_; }

    static const std::vector<std::string>& classNames();

    // BGR frame -> top1 + top3. Empty result (id -1) when no classifier.
    ClassResult classify(const cv::Mat& bgr);
    // BGR frame -> HxW uint8 mask {0 sky, 1 cloud, 2 contamination}.
    // Empty Mat when no segmenter.
    cv::Mat segment(const cv::Mat& bgr);

private:
    static void preprocess(const cv::Mat& bgr, int size, std::vector<float>& out,
                           int& w, int& h);
    Ort::Env env_;
    std::unique_ptr<Ort::Session> clsSession_;
    std::unique_ptr<Ort::Session> segSession_;
    std::string backend_ = "unloaded";
};
