# Ph8: CloudScope Desktop Requirements

## Functional
- FR-1: Live footage from USB camera (index), built-in camera, IP/RTSP stream, video file.
- FR-2: Per-frame cloud-type label + confidence (11 CCSN classes, Exp A3 weights).
- FR-3: Rectangle overlay: one bbox per detected cloud mass + type label.
- FR-4: Polygon overlay: contour per cloud mass + type label.
- FR-5: Symmetry overlay: filled-contour highlight + outline + label (the pro visual).
- FR-6: Runtime overlay-mode switch (Rect/Polygon/Symmetry) without restart.
- FR-7: Confidence threshold slider; masks below threshold hidden.
- FR-8: FPS + device (GPU/CPU) + frame-size HUD.
- FR-9: Snapshot save (annotated PNG) + session log.
- FR-10: Graceful camera disconnect/reconnect + CPU fallback if CUDA missing.

## Nonfunctional
- NFR-1: ≥15 FPS @720p on RTX 4050 (classifier 14ms + seg + render budget).
- NFR-2: No crash on missing camera/model; every error surfaced in UI log pane.
- NFR-3: Windows 10/11 x64 portable folder (windeployqt), no admin install.
- NFR-4: No fabricated detections: boxes ONLY from measured mask components;
  empty mask → explicit "no cloud detected" state.

## Out of scope (this session)
- YOLO26 detector training (needs box labels — Ph6).
- Cloud-height estimation (no paired data — prior verdict stands).
- Multi-camera grid (architecture supports it; single source ships first).
