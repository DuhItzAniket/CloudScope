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
cloudscope_app.exe --autotest SRC --frames N --shotdir DIR
                                :: live pipeline headless (threads+snapshot)
```

## Packaging checklist (agentic-run lessons — do not skip)
1. After `windeployqt`, ALSO copy: `platforms/qoffscreen.dll` (Qt plugins dir;
   windeployqt omits it → offscreen/headless runs abort instantly).
2. Copy OpenCV backends beside exe: `opencv_videoio_ffmpeg4110_64.dll` (video
   files) + `opencv_videoio_msmf4110_64.dll` (USB/builtin cameras). Without
   them, file sources yield 0 frames and cameras may misbehave.
3. Bundle `onnxruntime*.dll` beside exe (beats the stale System32 v1.17).
4. Verify: `--infertest` (numbers), `--autotest` clip (frames=N, snapshot),
   `--qmltest` offscreen (UI loads). See `.github/history/038_*`.

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
