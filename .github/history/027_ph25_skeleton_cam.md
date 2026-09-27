# Ph25: App Skeleton + CameraSource — DONE (verified on hardware)

- `desktop/app/` CMake project (Qt6 Core/Quick/Widgets + OpenCV + ORT),
  `desktop/tools/{env,cfg_app,build_app}.bat`.
- Modules: `CameraSource` (USB idx via MSMF / RTSP URL / file, probe 0..4),
  `FrameProvider` (Mat→QImage bridge), `AppController` (minimal),
  stubs for `OnnxInfer`/`CloudVision` (Ph26/27 fill in).
- CLI: `--camtest` verified on THIS machine: **Camera 0 OPEN 640x480,
  grabbed=5; cameras 1-4 n/a; exit 0**. MSMF backend + D3D11 accel active.
- Notes: default Ninja configure = Debug (parallel-plugin warnings harmless);
  switch to `-DCMAKE_BUILD_TYPE=Release` for perf builds (Ph31).
