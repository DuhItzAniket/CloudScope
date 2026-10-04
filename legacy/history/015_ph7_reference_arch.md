# Ph7: Reference Architecture (published projects, not invented)

Pattern converged from three shipped open-source apps:
- **STAR-REIN/ONNX-Detect** (PyQt6 + onnxruntime-gpu + OpenCV): CUDA-first ORT
  backend with CPU fallback, one-shot + real-time modes, camera detect/enable/
  resolution-select, MSMF backend on Windows, async model load, threshold sliders.
- **DarkShrill/FaceRecognition** (Qt6/QML + C++, MSVC, ORT 1.20 + CUDA/cuDNN,
  OpenCV 4.13): `Source → Controller → Engine → Worker → Pipeline
  (Detect→Landmark→Align→Recognize→DB) → Result → QML`; CPU/CUDA auto-fallback;
  overlays (boxes, names, confidence, landmarks); RTSP/webcam/file inputs.
- **cv-inspect** (CMake + Qt6/QML optional target): OpenCV default backend with
  extension boundary; `OpenCV frame → QImage → QQuickImageProvider → QML Image`.

## Adopted for CloudScope Desktop (C++, Qt6/QML, MSVC, CMake+Ninja)
- **Capture**: OpenCV `VideoCapture` (USB index / builtin / RTSP-IP / file) —
  one API for every source the user asked for. Worker thread, drop-oldest queue.
- **Inference**: ONNX Runtime C++ (CUDA EP → CPU fallback), two sessions:
  classifier (11 cloud types, 224²) + segmenter (sky/cloud/contamination mask).
- **Vision**: mask → connected components → per-cloud {bbox, polygon, area}.
- **Bridge**: `cv::Mat` → `QImage` → `QQuickImageProvider` → QML `Image`.
- **Overlays** (runtime switch): RECT (bbox+label) / POLYGON (contour+label) /
  SYMMETRY (filled contour highlight + outline + label) — all from the same mask.
- **HUD**: predicted type + confidence, FPS, device (GPU/CPU), frame stats.
