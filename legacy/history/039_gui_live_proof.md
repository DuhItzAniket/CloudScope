# GUI Live Proof + Layout Fix — DONE

## Layout bug (found via screenshot, fixed)
- RowLayout collapsed the video rect to zero width (panel took full width).
- Fix: anchor-based layout (video left fluid + fixed 330px panel). Verified by
  screenshot: video left, full control panel right.

## Debug-DLL lesson
- Debug builds link `opencv_world4110d.dll` (+debug videoio plugins); copy them
  beside dev-loop exes. Release/package unaffected.

## Live on-screen proof (screenshot, real display)
- `--autostart 0` with absolute model paths → worker ran, QML displayed frames.
- Frame shows room interior labeled "Ns (mask uncertain)" — honest output for a
  non-sky frame: classifier guesses, mask gate refuses boxes, badge shows.
- Version footer bumped 0.4.0-ph28 → 1.0.0.
- Screenshots contain the user → kept in gitignored `build/` only, never committed.

## --autotest flag (new, kept)
- Headless live-pipeline test: source → N frames → stats + snapshot + exit code.
- Verified: cam (10/10), mp4 clip (15/15, Cu .957).
