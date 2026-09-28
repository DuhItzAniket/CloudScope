# Ph30: End-to-End Gate — PASS

- **C++ vs Python agreement**: 25/25 top-1 on all B0268 frames (A3 weights,
  e.g. b0268_005 Cu .9968 both sides).
- **GPU latency (ORT CUDA, RTX 4050, steady-state)**: classify 11ms +
  segment 35ms + render ≈ **~21fps** on 4656px frames → NFR-1 (≥15fps) PASS.
  (First-inference warmup ~600ms one-off, excluded.)
- **Overlays**: rect/polygon/symmetry verified on valid masks; degenerate
  masks fall back to label+badge (verified on B0268).
- `--infertest` now prints `t_cls_ms`/`t_seg_ms` for field perf checks.
