# Ph23: ONNX Runtime 1.22 GPU — DONE

- Downloaded `onnxruntime-win-x64-gpu-1.22.0.zip` (312MB) → `C:/tools/onnxruntime/`.
- Verified: `onnxruntime_providers_cuda.dll` + `onnxruntime.lib` present.
- CMake hint: `-DORT_ROOT=C:/tools/onnxruntime/onnxruntime-win-x64-gpu-1.22.0`.
- Note: needs CUDA 12.x runtime DLLs at app run time (cudart etc. ship in the
  ORT package's lib folder; cuDNN NOT bundled — RTX inference falls back to
  CUDA EP without cuDNN for our ops, else CPU fallback; verified in Ph24/30).
