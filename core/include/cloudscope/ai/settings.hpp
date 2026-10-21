// The [ai] section of the configuration (P066).

#pragma once

#include <cloudscope/common/error.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace cloudscope::ai {

struct AiSettings {
    bool enabled = true;              // offer the ONNX Runtime engines as devices and run the model
    std::filesystem::path model_dir;  // a model package (model_card.json + one .onnx); empty: no model
    std::string provider = "auto";    // "auto" (CUDA, then DirectML, then CPU), "cuda", "directml" or "cpu"
    double rate_hz = 2.0;             // at most this many inferences per second (FR-AI-03)
    int threads = 0;                  // threads of the CPU provider; 0: the runtime's default
};

[[nodiscard]] Expected<AiSettings> ai_settings(const nlohmann::json& effective);

}  // namespace cloudscope::ai
