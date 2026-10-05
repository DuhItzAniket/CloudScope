// HAL: inference engines that run a STRATIA model (FR-AI-01 to FR-AI-03).
//
// An engine loads one model and maps named input tensors to named output tensors. stratia-contract v1 uses
// 32-bit float tensors only, so that is all this interface carries. Checking a model against the contract
// (model_card.json) is done above this layer (P065); the engine only reports what the model declares.
//
// One engine object stands for one way of executing a model (a CPU, a GPU through one execution provider):
// choosing between them is choosing between devices of the registry (FR-AI-02).
#pragma once

#include "cloudscope/hal/device.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cloudscope::hal {

struct TensorInfo {
    std::string name;
    std::vector<std::int64_t> shape;  // -1 for a dimension the model leaves open (batch size)
};

struct Tensor {
    std::string name;
    std::vector<std::int64_t> shape;
    std::vector<float> data;  // row-major; size is the product of `shape`
};

struct InferenceCapabilities {
    std::string provider;            // what executes the model: "CPU", "CUDA", "DirectML", "simulated", ...
    std::vector<TensorInfo> inputs;  // of the loaded model
    std::vector<TensorInfo> outputs;
};

class IInference : public IDevice {
public:
    // Loads a model, replacing any model loaded before. NotFound, Parse or Unsupported on failure;
    // the previous model then stays loaded. close() unloads the model.
    [[nodiscard]] virtual Expected<void> load(const std::filesystem::path& model) = 0;

    // Unavailable until a model is loaded.
    [[nodiscard]] virtual Expected<InferenceCapabilities> capabilities() const = 0;

    // Runs the model once and returns one tensor per declared output, in the declared order.
    // InvalidArgument if an input is missing, unknown, or has the wrong shape or size.
    // Blocks until the result is ready; callers run it on their own worker thread (FR-AI-03).
    [[nodiscard]] virtual Expected<std::vector<Tensor>> run(const std::vector<Tensor>& inputs) = 0;
};

// Number of elements a tensor of this shape holds; 0 if the shape is empty or a dimension is not positive.
[[nodiscard]] std::size_t element_count(const std::vector<std::int64_t>& shape);

}  // namespace cloudscope::hal
