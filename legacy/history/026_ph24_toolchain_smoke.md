# Ph24: Toolchain Smoke Test — PASS

- `desktop/smoke/` (main.cpp + CMakeLists) links Qt6::Core + OpenCV + onnxruntime.
- Built with MSVC 19.44 + Ninja, ran clean: Qt 6.9.3 / OpenCV 4.11.0 / ORT 1.22.0.
- GOTCHA (critical for Ph31 packaging): `C:\Windows\System32\onnxruntime.dll`
  (v1.17, OS-shipped) shadows PATH. Fix: bundle ORT DLLs beside the exe
  (app-dir wins DLL search). Verified: local copy → 1.22.0 loads.
- `desktop/tools/env.bat` standardizes the MSVC+CMake shell for all builds.
- `build/` added to .gitignore (never commit binaries).
