# Ph33: Acceptance Test — RESULTS

| Req | Expectation | Evidence | Status |
|-----|-------------|----------|--------|
| FR-1 sources | USB/builtin/IP/file | Cam0 live grab; file read ok; RTSP same API untested | PASS* |
| FR-2 label+conf | 11 types + conf | 25/25 C++=Python, Cu .997 | PASS |
| FR-3 rectangles | bbox per mass | LenghuSky overlays verified | PASS |
| FR-4 polygons | contour per mass | verified | PASS |
| FR-5 symmetry fill | outline+highlight | alpha-fill verified, looks pro | PASS |
| FR-6 mode switch | runtime, no restart | QML radios → controller prop | PASS (code; on-screen click = morning) |
| FR-7 conf slider | hides weak masks | minConf gates seg in worker | PASS (code) |
| FR-8 HUD | fps/device/size | props wired; fps measured 21 | PASS (code) |
| FR-9 snapshot/log | PNG + log pane | snapshot() + 100-line log | PASS (code) |
| FR-10 resilience | no crash, CPU fallback | missing-file honest fail; CUDA→CPU auto | PASS |
| NFR-1 ≥15fps | GPU 21fps measured | 11+35ms/frame | PASS |
| NFR-2 no silent fail | badges + log pane | mask-uncertain badge verified | PASS |
| NFR-3 portable | windeployqt folder | 236MB isolated run OK | PASS |
| NFR-4 no fake boxes | gate+fallback | B0268 fallback verified | PASS |

*RTSP untested (no server). UI click-paths verified offscreen-load only;
morning on-screen run is the human acceptance (user runs Start button).
