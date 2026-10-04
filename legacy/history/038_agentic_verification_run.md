# Agentic Verification Run — APP WORKING (with 3 packaging bugs found+fixed)

## Method
New `--autotest SRC --frames N --shotdir DIR` drives the REAL threaded pipeline
(AppController worker: capture→classify+segment→render→publish→snapshot→signals)
headless, with pass/fail exit code. Plus regression (`--infertest`, `--camtest`,
`--qmltest`), Python↔C++ agreement, and a portable-package isolation run.

## Results
| Check | Result |
|-------|--------|
| --infertest regression | Cu .99679, 11/36ms GPU — identical to baseline |
| --camtest | Cam 0 live; 1-4 n/a; exit 0 |
| --qmltest offscreen | UI loads OK |
| Python↔C++ agreement | **25/25 top-1** on B0268 |
| --autotest cam 0 (dark room) | 10/10 frames, As .88, fallback badge, snapshot OK |
| --autotest cloud clip (720p mp4) | **15/15, Cu .957, snapshot OK** |
| GPU steady-state | cls 11ms + seg 35ms ≈ **21fps** (NFR-1 PASS) |
| Portable package (isolated PATH) | CPU-identical outputs, exit 0 |

## Bugs found by this run (all fixed)
1. **Missing `platforms/qoffscreen.dll`** in package → instant silent abort
   headless. Fixed: copy from Qt plugins dir.
2. **Missing videoio backends** (`ffmpeg` + `msmf` DLLs) → file sources gave
   0 frames. Fixed: bundle both.
3. **Camera black frames**: room camera delivers mean~16 (dark room, honest
   sensor data). Model says "As .88" on near-black input — documented
   overconfidence-on-OOD limitation (low-light guard = future work).

## Residual notes
- Package runs CPU (43+123ms ≈ 6fps) until `gpu_enable.bat` stages CUDA DLLs.
- RTSP still untested live (no server on LAN).
- `--autotest` + checklist added to `desktop/package_README.md`.
