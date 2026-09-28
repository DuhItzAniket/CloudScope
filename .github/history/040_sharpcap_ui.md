# SharpCap-Style UI Upgrade — DONE (screenshot-verified)

## What changed
- **MenuBar**: File (Snapshot/Exit), Camera (Start/Stop/Rescan), View
  (overlay modes + reticle toggle), Help (About with model/backend info).
- **ToolBar**: Start/Stop/Snapshot, overlay-mode combo, live class+conf readout.
- **StatusBar replacement**: custom footer (StatusBar type missing in this Qt
  profile) — status | resolution | frame# | device.
- **Tabbed panel**: Source / Camera (exposure/gain/auto/resolution, live-applied
  by worker) / Display (overlay radios, reticle, conf slider, **luma histogram
  Canvas** @ every 5th frame) / Info (Now-grid + log).
- **Reticle**: crosshair + circle + thirds grid, pure QML overlay, toggleable.
- **Fusion dark style** + palette in `main.cpp` (native light controls replaced).
- **Backend**: `CameraSource::setProp/getProp`; worker applies staged camera
  settings per-loop; `frameStats` extended (frame size, frame#, 64-bin hist);
  controller exposes histogram/frameNo/frameW/H + camera Q_INVOKABLEs.

## Verification (all green)
- Clean build (Debug + Release), `--qmltest` UI loaded OK.
- `--autotest` clip regression: 8/8 frames (new signal signature intact).
- **On-screen screenshot**: dark workbench, live annotated frame
  ("As (mask uncertain)" on dark room input — honest), reticle, panel, footer.
- Package refreshed (new exe + windeployqt re-run + qmltest OK).

## Honest notes
- Camera knob ranges are camera-dependent; unsupported knobs are driver-ignored.
- Histogram is display-only (exposure decisions stay manual).
- RTSP still untested live.
