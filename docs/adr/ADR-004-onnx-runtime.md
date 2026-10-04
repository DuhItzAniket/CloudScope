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
