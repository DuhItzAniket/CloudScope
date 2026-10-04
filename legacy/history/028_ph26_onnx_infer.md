# Ph26: OnnxInfer Module — DONE (verified vs Python)

- `desktop/app/src/OnnxInfer.{h,cpp}`: CUDA EP → CPU fallback, manual NCHW
  preprocess bit-identical to Python, wide-char paths (ORT 1.22 Windows),
  dynamic-HW seg output mapped back to frame size.
- Fixes: ORT wchar_t paths; dangling AllocatedStringPtr cleanup.
- `--infertest B0268/b0268_005.jpg`: backend=cpu, **Cu 0.9968 / St / Ns —
  exact match to Python parity check**; mask 4656x3496 cloud_frac 0.947
  (same known seg daytime behavior).
- GOTCHA: torch-bundled CUDA DLLs are 11.x; ORT 1.22 needs 12.x
  (cublasLt64_12). GPU EP attempt → Ph27-side task via NVIDIA pip redists.
- Debug-build note: OpenCV debug plugin warnings harmless; Release in Ph31.
