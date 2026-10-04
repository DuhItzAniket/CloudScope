# CloudScope Desktop + Detection/Outline Plan — 34 Phases

## Honest Constraints (read first)
1. **No other-AI-agent contact exists.** There is no tool to reach other agents.
   Substitute: published reference architectures (STAR-REIN/ONNX-Detect PyQt6+ORT,
   DarkShrill Qt6/QML+ORT face-recognition, cv-inspect CMake+Qt6/QML) + literature.
2. **No box annotations exist** (CCSN=whole-image labels, LenghuSky=polygons,
   B0268=single-class frames). Training a YOLO26 *detector* honestly is impossible
   overnight. Verdict: **YOLO26 deferred** until box labels exist.
   Rectangles/polygons WILL be delivered — derived from a trained segmentation
   mask (connected components → bbox + contour + fill). Same visuals, honest provenance.
3. **"Cloud Symmetry" mode** = filled-contour highlight + outline (implemented from
   the cloud mask). No fabricated symmetry axis; it is a professional mask overlay.
4. **Fit guarantee**: quantitative under/overfit diagnostics (Ph2), not claims.

## Stage A — Environment + Model Health (Ph1-5)
- Ph1: PyTorch 2.5.1+cu121 verification (smoke train + repro Exp A3 eval, transformers warning check)
- Ph2: Under/overfit diagnostics — learning curves, train/val/test gaps, per-class bias, fit report
- Ph3: Metrics lock — final numbers table (baseline vs A3 vs ensemble)
- Ph4: Toolchain recon — winget packages, disk space, VS Build Tools path
- Ph5: Install MSVC Build Tools + CMake + Ninja

## Stage B — Research + Architecture Docs (Ph6-9)
- Ph6: YOLO26 viability verdict doc (why deferred, what would unblock it)
- Ph7: Qt6-QML + ONNX Runtime + OpenCV reference architecture doc
- Ph8: Requirements spec (functional FR-1..N, nonfunctional, camera matrix)
- Ph9: Full architecture doc (modules, threads, frame pipeline, overlay modes)

## Stage C — Segmentation Data (Ph10-13)
- Ph10: LabelMe→mask rasterizer (typo normalization: contination/contimination→contamination, clode→cloud)
- Ph11: Mask QA — coverage stats + visual sample sheet
- Ph12: Timestamp-based train/val split (anti-leakage) + split manifest
- Ph13: Commit seg tooling + QA artifacts

## Stage D — Segmentation Training (Ph14-18)
- Ph14: U-Net (pretrained encoder) training setup, 3-class (sky/cloud/contamination)
- Ph15: Train on RTX 4050 (primary long GPU job)
- Ph16: Eval — IoU per class, qualitative overlays, failure cases
- Ph17: Mask→visuals Python reference (bbox + polygon + symmetry-fill)
- Ph18: Commit seg model metrics + samples

## Stage E — Export (Ph19-20)
- Ph19: ONNX export (classifier already done; seg new) + parity check vs PyTorch
- Ph20: Model cards (input spec, classes, latency, limits)

## Stage F — C++ Toolchain (Ph21-24)
- Ph21: Qt6 headless install (aqtinstall, MSVC2022_64)
- Ph22: OpenCV prebuilt download + verify
- Ph23: ONNX Runtime GPU download + verify
- Ph24: Toolchain smoke test (CMake hello using Qt+OCV+ORT)

## Stage G — C++ App CloudScope Desktop (Ph25-31)
- Ph25: CMake project skeleton + `CameraSource` (USB/builtin/IP-RTSP/file)
- Ph26: `OnnxInfer` module (classifier + seg, CUDA→CPU fallback)
- Ph27: `OverlayRenderer` — rect mode / polygon mode / symmetry-fill mode
- Ph28: QML frontend (live view, source picker, mode switch, class+conf HUD, stats)
- Ph29: Camera matrix test (available sources enumerated, file fallback)
- Ph30: End-to-end test with real ONNX models on B0268 frames
- Ph31: Windows packaging (windeployqt, portable folder, run notes)

## Stage H — Validation + Report (Ph32-34)
- Ph32: Fit sign-off — under/overfit audit, wrong-data audit (label provenance)
- Ph33: Acceptance test (checklist FR/NFR vs implementation)
- Ph34: Final overnight report + push

## Commit Rule
Commit + push after every phase with phase doc under `.github/history/`.
Checkpoints (*.pth) and large binaries stay gitignored except deliverable `.onnx`.
