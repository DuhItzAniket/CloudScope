#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

// ONNX Runtime inference for classifier + segmenter (stub: Ph26 implements).
class OnnxInfer {
public:
    struct ClassResult {
        int id = -1;
        std::string label;
        float confidence = 0.0f;
        std::vector<std::pair<std::string, float>> top3;
    };

    OnnxInfer() = default;
    bool ready() const { return false; }
    std::string backend() const { return "unloaded"; }
};
