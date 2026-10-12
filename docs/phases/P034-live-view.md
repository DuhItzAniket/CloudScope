# P034 — Live view

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
The camera picture on screen with zoom, pan, fit and 1:1, frame-rate and drop indicators, smooth at the Arducam
B0268's full 4656 × 3496 MJPEG resolution on the laptop (FR-DSP-01).

## Requirements covered
FR-DSP-01; NFR-DATA-03 (simulated frames are marked in the indicator).

## Design notes
- `Viewport` (`viewport.hpp`): the pure geometry (fit, 1:1, free zoom around an anchor, pan with no empty space
  beyond a large image's edges, image ↔ widget mapping); tested without widgets.
- `PreviewPipeline` (`preview_pipeline.hpp`): a worker thread with a `Delivery::Latest` subscription to the frame
  hub; per frame: `decode_bgr8`, the statistics at most every 200 ms (P036), the display stretch and highlight on
  a preview copy, conversion to a `QImage`; the UI thread is told once per processed frame ("latest wins", never a
  queue), so the display shows the present and the camera is never slowed down (architecture 4.1).
- `LiveView` (`live_view.hpp`): a `QWidget` whose canvas is a `QOpenGLWidget` (Qt's OpenGL paint engine uploads the
  picture as a texture and the GPU scales it) or a software `QWidget` (`--renderer raster`, and the offscreen
  tests); both paint the same `LiveViewScene` through `paint_live_view()`. Large pictures are scaled without
  filtering in the software path; the OpenGL path filters on the GPU.
- Indicators: camera rate (`AcquisitionStats::recent_fps`), display rate (frames shown per second), lost frames
  (gaps in the sequence numbers), dropped (pool misses plus preview skips), decode time, picture size, SIMULATED.
- `CameraService` (`camera_service.hpp`): the camera from the UI's point of view; every device call runs on a
  serial worker thread, results come back as signals; the sequencer's recovery uses `recover_blocking()`.

## Work log
1. `viewport.cpp`, `preview_pipeline.cpp`, `live_view.cpp`, `camera_service.cpp`; the main window wires frame →
   live view → histogram and image tools.
2. Measured the B0268 at full resolution through the application (`--camera uvc:0c45:636d:1 --mode
   4656x3496@10/MJPEG --start --exit-after 30`, Release build, OpenGL): camera 9.32 fps, display 8.67 fps, 279 frames in 30 s, 278 shown, 0 lost, 0 pool misses, 0 skipped; decode 38.9 ms and processing 14.9 ms per frame (the camera's own rate at this mode is 9.6 fps, P020). The software canvas on the same camera: display 9.89 fps, 278 of 279 shown, decode 37.9 ms, processing 11.2 ms.
3. The same at 1920 × 1080: camera 27.66 fps, display 28.34 fps, 827 frames, 826 shown, 0 lost, 0 skipped; decode 5.0 ms, processing 1.6 ms.

## Verification
- `ui.` tests `[viewport]`: fit scale and centring, 1:1 mapping, zoom around an anchor keeps the anchored image
  point, pan clamping, scale clamping and inverse mapping, resize re-centres.
- 4656 × 3496 MJPEG on the laptop (work log 2): the display keeps up with the camera (every frame the camera delivered was decoded and shown, none lost or skipped, on both canvases; a first run computed the statistics on the full 16-megapixel picture and skipped 17 % of the frames, so the pipeline now reduces pictures above 2.5 megapixels before the statistics pass).
- Manual: wheel zoom around the cursor, drag pan, double-click 1:1 / fit, `F`, `1`, `+`, `-`; indicators on and
  off; simulated camera marked.

## Exit criteria
- [x] 4656×3496 MJPEG preview smooth on laptop (work log 2).

## Safety & failure-mode notes
- A frame that fails to decode is skipped and logged at debug level; the display keeps the last good picture.
- Without a usable OpenGL context (remote desktop, container) the software canvas is used; `--renderer`
  overrides the choice and the About box says which one is in use.

## Deviations & next phase
- QRhiWidget was not used: the offscreen CI platform has no graphics device, and Qt's OpenGL paint engine already
  gives GPU scaling with the same painting code as the software path.
- Next: P035 — Camera control panel.
