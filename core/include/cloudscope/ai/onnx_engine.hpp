// ONNX Runtime behind the inference HAL (P066, ADR-004, FR-AI-02).
//
// One `OnnxInference` device stands for one execution provider of the ONNX Runtime build CloudScope was linked
// with: "onnx:inference:cpu", "onnx:inference:directml" (Windows builds with DirectML), "onnx:inference:cuda"
// (builds whose runtime ships the CUDA provider). `OnnxDriver` enumerates the providers the runtime reports as
// available, in the preferred order of ADR-004 (CUDA, DirectML, CPU). Creating a session on a provider that the
// machine cannot serve (no DirectX 12 GPU, no CUDA libraries) fails at load() with Unavailable, and the caller
// moves on to the next device (InferenceService).
//
// Without ONNX Runtime (CLOUDSCOPE_ONNXRUNTIME=OFF) the driver enumerates nothing and load() is Unsupported.

#pragma once

#include <cloudscope/common/error.hpp>
#include <cloudscope/hal/inference.hpp>
#include <cloudscope/hal/registry.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope::ai {

enum class Provider : std::uint8_t { Cpu, DirectML, Cuda };

[[nodiscard]] std::string_view to_string(Provider provider);                        // "CPU", "DirectML", "CUDA"
[[nodiscard]] std::optional<Provider> provider_from_string(std::string_view text);  // "cpu", "directml", "cuda"
[[nodiscard]] std::string device_id(Provider provider);                             // "onnx:inference:cpu"

// The providers compiled into the linked ONNX Runtime, most preferred first. Empty without ONNX Runtime.
[[nodiscard]] std::vector<Provider> available_providers();
[[nodiscard]] std::string onnx_runtime_version();  // "" without ONNX Runtime
[[nodiscard]] bool onnx_runtime_available();

struct OnnxOptions {
    Provider provider = Provider::Cpu;
    int threads = 0;     // intra-op threads of the CPU provider; 0: the runtime's default
    int device_id = -1;  // DirectML: -1 = the high-performance GPU, else the DXGI adapter index; CUDA: device (0 if -1)
};

class OnnxInference final : public hal::IInference {
public:
    [[nodiscard]] static std::shared_ptr<OnnxInference> make(const OnnxOptions& options);
    ~OnnxInference() override;
    OnnxInference(const OnnxInference&) = delete;
    OnnxInference& operator=(const OnnxInference&) = delete;
    OnnxInference(OnnxInference&&) = delete;
    OnnxInference& operator=(OnnxInference&&) = delete;

    [[nodiscard]] const hal::DeviceInfo& info() const override { return info_; }
    [[nodiscard]] Expected<void> open() override;
    void close() override;
    [[nodiscard]] bool is_open() const override;

    [[nodiscard]] Expected<void> load(const std::filesystem::path& model) override;
    [[nodiscard]] Expected<hal::InferenceCapabilities> capabilities() const override;
    [[nodiscard]] Expected<std::vector<hal::Tensor>> run(const std::vector<hal::Tensor>& inputs) override;

    // A custom metadata entry of the loaded model (STRATIA embeds its model card under "stratia_model_card").
    [[nodiscard]] Expected<std::optional<std::string>> metadata(std::string_view key) const;
    [[nodiscard]] const OnnxOptions& options() const { return options_; }
    [[nodiscard]] std::filesystem::path model_file() const;  // empty when nothing is loaded

private:
    struct Session;
    explicit OnnxInference(const OnnxOptions& options);

    OnnxOptions options_;
    hal::DeviceInfo info_;
    mutable std::mutex mutex_;
    bool open_ = false;
    std::unique_ptr<Session> session_;
};

class OnnxDriver final : public hal::IDriver {
public:
    [[nodiscard]] static std::shared_ptr<OnnxDriver> make(int threads = 0);
    [[nodiscard]] std::string_view name() const override { return "onnx"; }
    [[nodiscard]] std::vector<hal::DeviceInfo> enumerate() override;
    [[nodiscard]] Expected<std::shared_ptr<hal::IDevice>> create(std::string_view id) override;

private:
    explicit OnnxDriver(int threads) : threads_(threads) {}
    int threads_;
};

}  // namespace cloudscope::ai
