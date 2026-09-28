# Ph31: Release + Portable Packaging — DONE

- Release build (Ninja, MSVC): `build/app-release/cloudscope_app.exe`.
- `windeployqt` (qmldir-aware) + `opencv_world4110.dll` + ORT DLLs +
  both ONNX models → `package/CloudScope/` = **236MB**, no install/admin.
- Isolation test (bare Windows PATH): runs, CPU inference bit-identical
  (Cu .99679), sane=0 fallback correct.
- GPU is opt-in: `desktop/tools/gpu_enable.bat` stages the 2GB CUDA/cuDNN set
  (kept out of the portable folder; recipe in 029).
- `package/` gitignored; full rebuild recipe in `desktop/package_README.md`.
- Leftover: Debug `build/app` still the dev loop; Release is the ship config.
