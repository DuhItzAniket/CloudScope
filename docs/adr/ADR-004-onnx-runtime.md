# ADR-004 — ONNX Runtime for STRATIA inference

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
STRATIA is trained in PyTorch and exported to ONNX (STRATIA P096). CloudScope must run it on Windows laptops (NVIDIA GPU or CPU) and on a Raspberry Pi 5 (CPU, optionally an accelerator). The prototype already ran ONNX Runtime with the CUDA provider at 21 fps.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| **ONNX Runtime (MIT)** | One model file everywhere; CUDA, DirectML and CPU providers; prebuilt packages for Windows x64 (CPU, CUDA 12/13) and **Linux aarch64** (verified in release 1.30.0, 2026-09-10, via the GitHub releases API); prototype experience | Large GPU runtime DLLs on Windows |
| OpenCV DNN | Already a dependency | Weaker operator coverage for ViT models; slower |
| TensorRT | Fastest on NVIDIA | NVIDIA-only; not on the Pi |
| LibTorch | Matches training | Very large; no Pi-friendly build |

## Decision
Use **ONNX Runtime** through its C++ API. Execution-provider order: CUDA → DirectML (Windows, via the `Microsoft.ML.OnnxRuntime.DirectML` package) → CPU. On the Raspberry Pi: CPU (with the INT8 model from STRATIA P096); accelerators such as the Hailo AI Kit are evaluated in P066 and would need a separate compiled model.

## Consequences
- The model card declares the contract version and input/output names; CloudScope validates it before loading (FR-AI-01).
- GPU runtime DLLs are kept out of the portable package and installed by a helper, as in the prototype.
- Inference runs on its own thread with latest-frame-only semantics (architecture §4.1).

## Implementation notes (P066, 2026-10-21)

- ONNX Runtime 1.24.4 is fetched at configure time (`cmake/CloudScopeDependencies.cmake`): on Windows the NuGet
  package `Microsoft.ML.OnnxRuntime.DirectML` (one `onnxruntime.dll` with the CPU and DirectML providers) and its
  dependency `Microsoft.AI.DirectML` (`DirectML.dll`); on Linux the release archive (CPU provider). The CUDA
  provider is not shipped: a runtime whose `onnxruntime` reports it gets an `onnx:inference:cuda` device, nothing
  else changes. `CLOUDSCOPE_ONNXRUNTIME=OFF` builds without the engine (the driver enumerates nothing).
- One `OnnxInference` device per provider (`core/ai/onnx_engine`); `InferenceService` picks the first of CUDA,
  DirectML, CPU that loads the model and declares the contract's tensors, and reports it (FR-AI-02).
- Latest-frame-only: `InferenceService::submit()` never blocks; the worker takes the newest frame, a replaced
  frame is counted as dropped; `ai.rate_hz` bounds the rate (FR-AI-03).
- DirectML.dll is a proprietary redistributable: the licence policy gained `allowed_redistributable`, the text
  travels in `packaging/licences/`.
