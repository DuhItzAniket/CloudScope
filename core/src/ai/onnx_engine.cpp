#include "cloudscope/ai/onnx_engine.hpp"

#include "cloudscope/common/log.hpp"

#include <fmt/format.h>

#if CLOUDSCOPE_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#if CLOUDSCOPE_HAS_DIRECTML
#include <dml_provider_factory.h>
#endif
#endif

#if CLOUDSCOPE_HAS_DIRECTML
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <cctype>
#include <system_error>

namespace cloudscope::ai {

namespace {

std::string lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

std::string_view to_string(Provider provider)
{
    switch (provider) {
    case Provider::Cpu:
        return "CPU";
    case Provider::DirectML:
        return "DirectML";
    case Provider::Cuda:
        return "CUDA";
    }
    return "CPU";
}

std::optional<Provider> provider_from_string(std::string_view text)
{
    const std::string key = lower(text);
    if (key == "cpu") {
        return Provider::Cpu;
    }
    if (key == "directml" || key == "dml") {
        return Provider::DirectML;
    }
    if (key == "cuda") {
        return Provider::Cuda;
    }
    return std::nullopt;
}

std::string device_id(Provider provider)
{
    return "onnx:inference:" + lower(to_string(provider));
}

#if CLOUDSCOPE_HAS_ONNXRUNTIME

namespace {

Ort::Env& ort_env()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cloudscope");
    return env;
}

std::vector<std::int64_t> declared_shape(const Ort::TypeInfo& info)
{
    const auto tensor = info.GetTensorTypeAndShapeInfo();
    std::vector<std::int64_t> shape = tensor.GetShape();
    for (std::int64_t& d : shape) {
        if (d < 0) {
            d = -1;  // ONNX Runtime reports symbolic dimensions as -1 already; keep one spelling
        }
    }
    return shape;
}

std::string shape_text(const std::vector<std::int64_t>& shape)
{
    std::string out = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        out += (i != 0U ? ", " : "") + std::to_string(shape[i]);
    }
    return out + "]";
}

}  // namespace

#if CLOUDSCOPE_HAS_DIRECTML
std::string utf8_of(const wchar_t* wide)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), length, nullptr, nullptr);
    return out;
}

struct DxgiAdapter {
    int index = 0;     // DXGI enumeration index, what the DirectML provider calls the device id
    std::string name;  // empty when nothing was found
};

// The adapter DirectML runs on: the one with that DXGI index (device_id >= 0), or the hardware adapter with the
// most dedicated video memory. Adapter 0 is the display adapter, which on a laptop with an integrated and a
// discrete GPU is the integrated one, and Windows' "high performance" preference followed the power policy
// rather than the hardware on the development laptop (the real model ran five times slower on the integrated
// GPU than on the CPU). Dedicated memory separates the two unambiguously.
DxgiAdapter select_dxgi_adapter(int device_id)
{
    DxgiAdapter chosen{.index = std::max(device_id, 0), .name = {}};
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return chosen;
    }
    SIZE_T best_memory = 0;
    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, &adapter))) {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) {
            continue;
        }
        const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        logger("ai").info("DXGI adapter {}: {} ({} MB dedicated, {} MB shared{})", i, utf8_of(description.Description),
                          description.DedicatedVideoMemory >> 20, description.SharedSystemMemory >> 20,
                          software ? ", software" : "");
        if (device_id >= 0) {
            if (static_cast<int>(i) == device_id) {
                chosen.name = utf8_of(description.Description);
            }
        } else if (!software && (chosen.name.empty() || description.DedicatedVideoMemory > best_memory)) {
            best_memory = description.DedicatedVideoMemory;
            chosen.index = static_cast<int>(i);
            chosen.name = utf8_of(description.Description);
        }
    }
    return chosen;
}
#endif

struct OnnxInference::Session {
    Ort::Session session;
    std::string provider_label;
    std::filesystem::path file;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<hal::TensorInfo> inputs;
    std::vector<hal::TensorInfo> outputs;

    Session(Ort::Session s, std::filesystem::path path) : session(std::move(s)), file(std::move(path)) {}
};

std::vector<Provider> available_providers()
{
    std::vector<Provider> out;
    std::vector<std::string> names;
    try {
        names = Ort::GetAvailableProviders();
    } catch (const Ort::Exception&) {
        return out;
    }
    const auto has = [&](std::string_view name) { return std::ranges::find(names, name) != names.end(); };
    if (has("CUDAExecutionProvider")) {
        out.push_back(Provider::Cuda);
    }
#if CLOUDSCOPE_HAS_DIRECTML
    if (has("DmlExecutionProvider")) {
        out.push_back(Provider::DirectML);
    }
#endif
    if (has("CPUExecutionProvider")) {
        out.push_back(Provider::Cpu);
    }
    return out;
}

std::string onnx_runtime_version()
{
    return OrtGetApiBase()->GetVersionString();
}

bool onnx_runtime_available()
{
    return true;
}

Expected<void> OnnxInference::load(const std::filesystem::path& model)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(model, fs_error)) {
        return fail(ErrorCode::NotFound, fmt::format("model file {} does not exist", model.string()));
    }
    try {
        Ort::SessionOptions options;
        std::string adapter;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (options_.threads > 0) {
            options.SetIntraOpNumThreads(options_.threads);
        }
        switch (options_.provider) {
        case Provider::Cpu:
            break;
        case Provider::DirectML:
#if CLOUDSCOPE_HAS_DIRECTML
            options.DisableMemPattern();
            options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            // DirectML compiles its kernels for fixed shapes: with the contract's open batch axis it would fall
            // back to slow paths on every run. CloudScope always runs one frame at a time.
            options.AddFreeDimensionOverrideByName("batch", 1);
            {
                const DxgiAdapter chosen = select_dxgi_adapter(options_.device_id);
                Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, chosen.index));
                adapter = chosen.name;
            }
            break;
#else
            return fail(ErrorCode::Unsupported, "this build has no DirectML provider");
#endif
        case Provider::Cuda: {
            OrtCUDAProviderOptions cuda{};
            cuda.device_id = std::max(options_.device_id, 0);
            options.AppendExecutionProvider_CUDA(cuda);
            adapter = "device " + std::to_string(cuda.device_id);
            break;
        }
        }
        auto session = std::make_unique<Session>(Ort::Session(ort_env(), model.native().c_str(), options), model);
        session->provider_label =
            std::string(to_string(options_.provider)) + (adapter.empty() ? "" : " (" + adapter + ")");
        const Ort::AllocatorWithDefaultOptions allocator;
        for (std::size_t i = 0; i < session->session.GetInputCount(); ++i) {
            const auto name = session->session.GetInputNameAllocated(i, allocator);
            session->input_names.emplace_back(name.get());
            session->inputs.push_back(
                {.name = name.get(), .shape = declared_shape(session->session.GetInputTypeInfo(i))});
        }
        for (std::size_t i = 0; i < session->session.GetOutputCount(); ++i) {
            const auto name = session->session.GetOutputNameAllocated(i, allocator);
            session->output_names.emplace_back(name.get());
            session->outputs.push_back(
                {.name = name.get(), .shape = declared_shape(session->session.GetOutputTypeInfo(i))});
        }
        session_ = std::move(session);
    } catch (const Ort::Exception& error) {
        ErrorCode code = ErrorCode::Unavailable;
        if (error.GetOrtErrorCode() == ORT_NO_SUCHFILE) {
            code = ErrorCode::NotFound;
        } else if (error.GetOrtErrorCode() == ORT_INVALID_PROTOBUF || options_.provider == Provider::Cpu) {
            code = ErrorCode::Parse;
        }
        return fail(code, fmt::format("ONNX Runtime could not load {} on {}: {}", model.string(),
                                      to_string(options_.provider), error.what()));
    } catch (const std::exception& error) {
        return fail(ErrorCode::Internal, fmt::format("loading {}: {}", model.string(), error.what()));
    }
    logger("ai").info("model {} loaded on {} (ONNX Runtime {})", model.filename().string(), session_->provider_label,
                      onnx_runtime_version());
    return {};
}

Expected<hal::InferenceCapabilities> OnnxInference::capabilities() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (!session_) {
        return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
    }
    return hal::InferenceCapabilities{
        .provider = session_->provider_label, .inputs = session_->inputs, .outputs = session_->outputs};
}

Expected<std::vector<hal::Tensor>> OnnxInference::run(const std::vector<hal::Tensor>& inputs)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (!session_) {
        return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
    }
    if (inputs.size() != session_->inputs.size()) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("the model takes {} inputs, {} were given", session_->inputs.size(), inputs.size()));
    }
    std::vector<const hal::Tensor*> ordered(session_->inputs.size(), nullptr);
    for (const hal::Tensor& tensor : inputs) {
        const auto it = std::ranges::find(session_->input_names, tensor.name);
        if (it == session_->input_names.end()) {
            return fail(ErrorCode::InvalidArgument, fmt::format("the model has no input named '{}'", tensor.name));
        }
        const auto index = static_cast<std::size_t>(it - session_->input_names.begin());
        const hal::TensorInfo& declared = session_->inputs[index];
        if (tensor.shape.size() != declared.shape.size()) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format("input '{}' has shape {}, the model wants {}", tensor.name,
                                    shape_text(tensor.shape), shape_text(declared.shape)));
        }
        for (std::size_t d = 0; d < declared.shape.size(); ++d) {
            if (tensor.shape[d] <= 0 || (declared.shape[d] != -1 && declared.shape[d] != tensor.shape[d])) {
                return fail(ErrorCode::InvalidArgument,
                            fmt::format("input '{}' has shape {}, the model wants {}", tensor.name,
                                        shape_text(tensor.shape), shape_text(declared.shape)));
            }
        }
        if (tensor.data.size() != hal::element_count(tensor.shape)) {
            return fail(ErrorCode::InvalidArgument, fmt::format("input '{}' holds {} values for shape {}", tensor.name,
                                                                tensor.data.size(), shape_text(tensor.shape)));
        }
        ordered[index] = &tensor;
    }
    try {
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> values;
        std::vector<std::vector<float>> buffers;  // ONNX Runtime wants writable input memory
        std::vector<const char*> input_names;
        std::vector<const char*> output_names;
        values.reserve(ordered.size());
        buffers.reserve(ordered.size());
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const hal::Tensor& tensor = *ordered[i];
            buffers.push_back(tensor.data);
            values.push_back(Ort::Value::CreateTensor<float>(memory, buffers.back().data(), buffers.back().size(),
                                                             tensor.shape.data(), tensor.shape.size()));
            input_names.push_back(session_->input_names[i].c_str());
        }
        for (const std::string& name : session_->output_names) {
            output_names.push_back(name.c_str());
        }
        std::vector<Ort::Value> results =
            session_->session.Run(Ort::RunOptions{nullptr}, input_names.data(), values.data(), values.size(),
                                  output_names.data(), output_names.size());
        std::vector<hal::Tensor> outputs;
        outputs.reserve(results.size());
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto info = results[i].GetTensorTypeAndShapeInfo();
            hal::Tensor out{.name = session_->output_names[i], .shape = info.GetShape(), .data = {}};
            const auto count = info.GetElementCount();
            const auto* data = results[i].GetTensorData<float>();
            out.data.assign(data, data + count);
            outputs.push_back(std::move(out));
        }
        return outputs;
    } catch (const Ort::Exception& error) {
        return fail(ErrorCode::Io,
                    fmt::format("ONNX Runtime run failed on {}: {}", to_string(options_.provider), error.what()));
    }
}

Expected<std::optional<std::string>> OnnxInference::metadata(std::string_view key) const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    if (!session_) {
        return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
    }
    try {
        const Ort::AllocatorWithDefaultOptions allocator;
        const Ort::ModelMetadata metadata = session_->session.GetModelMetadata();
        const auto value = metadata.LookupCustomMetadataMapAllocated(std::string(key).c_str(), allocator);
        if (!value) {
            return std::optional<std::string>{};
        }
        return std::optional<std::string>(value.get());
    } catch (const Ort::Exception& error) {
        return fail(ErrorCode::Io, fmt::format("reading model metadata: {}", error.what()));
    }
}

#else  // no ONNX Runtime in this build

struct OnnxInference::Session {};

std::vector<Provider> available_providers()
{
    return {};
}

std::string onnx_runtime_version()
{
    return {};
}

bool onnx_runtime_available()
{
    return false;
}

Expected<void> OnnxInference::load(const std::filesystem::path& model)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(model, fs_error)) {
        return fail(ErrorCode::NotFound, fmt::format("model file {} does not exist", model.string()));
    }
    return fail(ErrorCode::Unsupported, "this build of CloudScope has no ONNX Runtime");
}

Expected<hal::InferenceCapabilities> OnnxInference::capabilities() const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
}

Expected<std::vector<hal::Tensor>> OnnxInference::run(const std::vector<hal::Tensor>& /*inputs*/)
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
}

Expected<std::optional<std::string>> OnnxInference::metadata(std::string_view /*key*/) const
{
    const std::lock_guard lock(mutex_);
    if (!open_) {
        return not_open(info_);
    }
    return fail(ErrorCode::Unavailable, fmt::format("inference engine '{}' has no model loaded", info_.id));
}

#endif

// ---------------------------------------------------------------------------------------------- common parts

OnnxInference::OnnxInference(const OnnxOptions& options)
    : options_(options),
      info_{.id = device_id(options.provider),
            .kind = hal::DeviceKind::Inference,
            .name = fmt::format("ONNX Runtime ({})", to_string(options.provider)),
            .driver = "onnx",
            .simulated = false}
{
}

OnnxInference::~OnnxInference() = default;

std::shared_ptr<OnnxInference> OnnxInference::make(const OnnxOptions& options)
{
    return std::shared_ptr<OnnxInference>(new OnnxInference(options));
}

Expected<void> OnnxInference::open()
{
    const std::lock_guard lock(mutex_);
    open_ = true;
    return {};
}

void OnnxInference::close()
{
    const std::lock_guard lock(mutex_);
    session_.reset();
    open_ = false;
}

bool OnnxInference::is_open() const
{
    const std::lock_guard lock(mutex_);
    return open_;
}

std::filesystem::path OnnxInference::model_file() const
{
    const std::lock_guard lock(mutex_);
#if CLOUDSCOPE_HAS_ONNXRUNTIME
    return session_ ? session_->file : std::filesystem::path();
#else
    return {};
#endif
}

std::shared_ptr<OnnxDriver> OnnxDriver::make(int threads)
{
    return std::shared_ptr<OnnxDriver>(new OnnxDriver(threads));
}

std::vector<hal::DeviceInfo> OnnxDriver::enumerate()
{
    std::vector<hal::DeviceInfo> out;
    for (const Provider provider : available_providers()) {
        out.push_back({.id = device_id(provider),
                       .kind = hal::DeviceKind::Inference,
                       .name = fmt::format("ONNX Runtime ({})", to_string(provider)),
                       .driver = "onnx",
                       .simulated = false});
    }
    return out;
}

Expected<std::shared_ptr<hal::IDevice>> OnnxDriver::create(std::string_view id)
{
    for (const Provider provider : available_providers()) {
        if (device_id(provider) == id) {
            return std::shared_ptr<hal::IDevice>(
                OnnxInference::make({.provider = provider, .threads = threads_, .device_id = -1}));
        }
    }
    return fail(ErrorCode::NotFound, fmt::format("no ONNX Runtime device '{}'", id));
}

}  // namespace cloudscope::ai
