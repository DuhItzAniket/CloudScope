# Ph26b: GPU Inference Enabled in C++ — DONE

## Problem chain (all resolved)
1. System32 `onnxruntime.dll` v1.17 shadowed ours → bundle DLLs beside exe.
2. CUDA EP missing `cublasLt64_12.dll` → torch ships CUDA **11.x** (incompatible).
3. Then missing `cufft64_11.dll` (cuFFT major 11 ships with CUDA **12**, not cu11
   wheels which carry cufft64_10).

## Fix (reproducible, no admin)
```bat
pip download nvidia-cublas-cu12 nvidia-cuda-runtime-cu12 nvidia-cudnn-cu12 nvidia-cufft-cu12 --no-deps -d C:/tools/nvredist
:: unzip wheels, copy nvidia/*/bin/*.dll next to the exe
```
Result: `--infertest` reports **backend=cuda**, identical outputs (Cu .9968).

## Cost note
Full NVIDIA DLL set beside exe ≈ **2.0GB** (cublasLt 669MB + cudnn engines
552MB dominate). Ph31 packaging will ship a trimmed set and document the
CUDA-12.x prerequisite alternative.

## Latency reference (PyTorch GPU, RTX 4050)
- Classifier 224²: **5.5ms**; Segmenter 512²: **7.4ms** → ~13ms + overhead.
- NFR-1 (≥15fps @720p) is achievable on the GPU path.
