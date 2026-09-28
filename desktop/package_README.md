# CloudScope Desktop — Portable Package

Built: `desktop/tools/{cfg_app_release,build_app_release}.bat` (MSVC, Release).
Deployed with `windeployqt` + OpenCV world + ORT + both ONNX models.

## Run (no install, no admin)
```bat
cd package\CloudScope
cloudscope_app.exe                 :: GUI: pick source, Start
cloudscope_app.exe --camtest       :: camera matrix probe
cloudscope_app.exe --infertest IMG :: headless classify+segment
cloudscope_app.exe --qmltest       :: UI smoke test (needs display or offscreen)
```

## GPU inference (optional, ~2GB CUDA DLLs)
The portable folder ships CPU-capable. For CUDA:
```bat
gpu_enable.bat   :: copies CUDA 12 + cuDNN DLLs from C:/tools/nvredist (dev machine)
```
or install the CUDA 12.x toolkit + cuDNN 9 on the target PC. The app prints
`backend=cuda|cpu` at startup — trust that line, not assumptions.

## Contents
- `cloudscope_app.exe`, Qt6 DLLs + QML imports, `opencv_world4110.dll`,
  `onnxruntime.dll` (+providers), `models/*.onnx` (classifier 16.9MB, seg 57.5MB).
- `package/` is gitignored (binaries); rebuild from source per above.
