# Ph27: CloudVision Overlays — DONE (verified headless)

- `CloudVision::{extract,sane,render}` real: connected components (0.2% min area),
  approxPolyDP polygons, sanity gate (largest <80% frame).
- Modes: **rect** (bbox+label), **polygon** (contour+label),
  **symmetry** (0.35-alpha fill + white outline + label).
- `--infertest --out` verified both branches on GPU:
  - LenghuSky overcast: objects=1 sane=1 → all 3 overlays saved, look professional.
  - B0268 day: objects=1 sane=0 → **label-only + "(mask uncertain)" badge**
    (degenerate mask correctly suppressed — no fake fullscreen boxes).
- Known: night fisheye can yield thin edge-artifact boxes (mask quality bound,
  documented in 021). Label placement clips at frame edges (minor, Ph28 polish).
