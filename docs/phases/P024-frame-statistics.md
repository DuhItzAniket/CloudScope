# P024 — Frame statistics

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
The numbers the sky auto-exposure, the operator and the sidecar need from a picture: histogram, mean, median,
clipping and dark fractions, noise estimate, sharpness, a saturation map and the position of the Sun.

## Requirements covered
FR-CAM-09 (frame statistics), groundwork for FR-CAM-05/06 (auto-exposure, HDR) and the sidecar of P027.

## Design notes
`core/include/cloudscope/capture/statistics.hpp`: `compute_statistics(image)` on 8-bit luma (BT.601 from BGR):
- histogram (256 bins), mean, median and any percentile from the histogram, standard deviation;
- clipped fraction (luma ≥ 250) and dark fraction (≤ 5), thresholds adjustable;
- noise: median absolute difference of horizontally adjacent pixels / 0.6745 / √2 (robust against edges);
- sharpness: variance of the Laplacian;
- Sun: the largest connected component of clipped pixels with at least 20 px and a bounding-box fill ≥ 0.45 (a
  disc fills 0.79; a ragged clipped cloud edge or a thin bright line does not), with centroid, equivalent radius and
  area.
`saturation_map()` gives the clipped mask; `luma_of()` the luma.

## Work log
1. Module and tests (`tests/unit/test_decode_statistics.cpp`): flat, noisy, gradient and colour pictures; a Sun
   disc next to a clipped edge; a ragged line and a 1-px dot that must not count as the Sun.
2. Used by `camtool stream` (live luma, clipping, noise, Sun) and by P025.

## Verification
- Unit tests: mean/median/std exact on flat and gradient pictures; noise estimate within 35 % of a planted
  σ = 6; clipped and dark fractions exact (6/256 each on a 0..255 ramp); red = luma 76 ± 1; Sun found at (210, 70)
  with radius 15 ± 1 and fill > 0.7; a diagonal line and a 1-px dot are not the Sun.
- On the B0268 (`camtool stream`, desk scene): statistics computed per frame at 1920x1080 alongside decoding at
  28 fps (numbers in P022).

## Exit criteria
- [x] Unit tests on synthetic frames.

## Risks / notes
- The Sun detector sees "a round clipped blob"; a clipped round lamp in a desk scene passes as well. For sky
  frames this is the intended behaviour; the sidecar records radius and fill so later analysis can judge.

## Next phase
P025 — Sky auto-exposure + HDR.
