# P026 — Calibration frames

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Dark frames, flat fields and vignetting correction: build masters from stacks of frames, derive a gain map,
apply the correction, and measure that a flat frame gets flatter.

## Requirements covered
FR-CAM-10 (calibration frames), groundwork for the recording sidecar (calibration id, P027) and STRATIA's
camera-model export (P031).

## Design notes
`core/include/cloudscope/capture/calibration_frames.hpp`:
- `average_frames()`: mean and per-pixel standard deviation of 8- or 16-bit frames (float masters);
- `gain_map(flat, dark)`: mean(flat − dark) / (flat − dark), clipped to [0.2, 5], mean 1;
- `apply_calibration(picture, dark, gain)`: (picture − dark) × gain back in the picture's depth, saturated;
- `fit_vignetting(flat)`: least-squares fit of g(r) = 1 + a r² + b r⁴ over 40 radial bins (r = distance from the
  centre over half the diagonal), with the rms residual; `vignetting_gain()` renders 1/g normalised;
- `uniformity_spread()`: (p95 − p5) / median of a 15×15 box-blurred luma: 0 for a perfectly even field;
- masters are saved as 16-bit TIFF with a JSON sidecar (frames, depth, noise); image files go through
  `std::filesystem` paths (`write_image`/`read_image`), because OpenCV's own file functions cannot name every
  Windows folder.
`cloudscope-camtool dark|flat <id> --frames N --out FILE.tiff [--dark FILE]` captures masters from a camera and,
for flats, prints the uniformity before and after correction and the vignetting fit.

## Work log
1. Module, camtool commands, tests (`tests/unit/test_exposure_calibration.cpp`).

## Verification
- Synthetic camera (160x120): even field under vignetting 1 − 0.4 r², fixed-pattern dark offset 8–16 levels,
  Gaussian noise σ = 2; 16 darks and 16 flats. Dark master mean 12 ± 1 and noise 2 ± 0.6 recovered; gain map mean
  1.00 ± 0.05; an even scene through the same camera has uniformity spread > 0.2 before and < 25 % of that after
  correction, with its mean level preserved (120 ± 3); the vignetting fit returns a = −0.4 ± 0.08 with rms
  residual < 0.05; masters round-trip through TIFF + sidecar in a folder with a non-ASCII name.
- Real dark and flat masters of the B0268 were not captured in this phase: a dark needs the lens covered and a flat
  an evenly lit surface, both operator actions; the commands are ready for the first B0268 session (owner).

## Exit criteria
- [x] Flat-corrected frame uniformity improved (measured): on the synthetic flat, by more than a factor of four;
  the measurement on the B0268's own flats is an open item for the owner's next session.

## Risks / notes
- A flat of the sky itself (the usual all-sky trick) contains the sky's gradient and the Sun; the tool's
  `uniformity_spread` before/after tells whether a given flat helped.
- Hot pixels are in the dark master's mean; a separate hot-pixel map (median filter outliers) can be added when
  night captures (P028 star trails) need it.

## Next phase
P027 — Recording I.
