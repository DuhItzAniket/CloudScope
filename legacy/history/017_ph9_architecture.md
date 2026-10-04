# Ph9: CloudScope Desktop Architecture (C++17, Qt6/QML, CMake+Ninja, MSVC)

```
Source (USB idx | builtin | RTSP URL | file)
  │ cv::VideoCapture (MSMF on Windows)
  ▼
CaptureWorker (QThread) ──drop-oldest queue──▶ FrameBus (latest cv::Mat + ts)
  │                                                     │
  │ QML UI ◀── QQuickImageProvider ── annotated QImage ◀┘
  │   │                        ▲
  │   │ signals/slots          │ OverlayRenderer
  │   ▼                        │  (RECT bbox / POLYGON contour / SYMMETRY fill+outline)
  │ Controller                  │
  │   ├── source picker, mode switch, conf slider, snapshot, log pane
  │   ▼
  │ InferWorker (QThread)
  │   ├── OnnxInfer: classifier.onnx (11 types) + seg.onnx (3ch mask)
  │   ├── ORT CUDA EP → CPU fallback; device shown in HUD
  │   └── Vision: argmax mask → cloud components → {bbox, polygon, area, conf}
  ▼
SessionLog (detections.jsonl + snapshots/)
```

## Modules (`desktop/src/`)
- `CameraSource.{h,cpp}` — open(url|index|file), read loop, reconnect, resolution probe.
- `OnnxInfer.{h,cpp}` — letterbox/normalize preprocess, two sessions, softmax, argmax.
- `CloudVision.{h,cpp}` — components, min-area filter, polygon approx, fill overlay.
- `FrameProvider.{h,cpp}` — QQuickImageProvider bridge (Mat→QImage).
- `AppController.{h,cpp}` — owns threads, exposes QML properties/slots.
- `main.cpp` + `qml/Main.qml` — window, video view, controls, HUD, log.

## Threading
UI thread never blocks: capture and inference each own a QThread;
annotated frames cross via queued signals (deep-copied QImage).

## Data contracts
- Classifier in: 224x224 RGB float32 NCHW, ImageNet norm. Out: 11 logits.
- Segmenter in: 512x512 RGB float32 NCHW. Out: 3-class logits → mask.
- Overlay modes are pure functions of (mask, class, conf, threshold).
