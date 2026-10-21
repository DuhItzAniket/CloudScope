# P067 — Metadata provider

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Build the model's geometry inputs, the ray map and the Sun metadata, from the active calibration, pose, time and
site, and mark them unknown when any of them is unavailable (FR-AI-04).

## Requirements covered
FR-AI-04.

## Design notes
- `core/ai/geometry_inputs`: `ray_map()` computes, at the centre of every patch of the resized model input, the
  unit viewing direction in the Sun-aligned frame (z up, x toward the Sun's azimuth, y = z × x) and a valid flag,
  exactly as STRATIA's reference implementation (`stratia/geometry/raymap.py`): the contract's full-frame resize
  (pixel-centre convention), `pixel_to_ray` of the P031 camera model, the camera orientation of the overlays
  (`SkyProjection::camera_to_enu()`: the roll first, then the pointing), the Sun's azimuth and apparent elevation,
  an optional camera mask sampled on a 4 × 4 lattice per patch, and a minimum elevation. `meta_vector()` is
  `[cos(Sun zenith), sin(Sun zenith), 1]`. `geometry_inputs()` returns the all-zero, invalid tensor when the
  calibration, the pose or the Sun is missing: the model was trained with geometry dropout and treats zeros as
  "unknown".
- Cross-repository vectors: STRATIA's `scripts/make_ray_map_reference.py` computes the ray map of the nominal
  B0268 camera for two poses given in CloudScope's convention (a rolled zenith camera; a camera tilted to 35°
  with the Sun low) and the matching meta vectors; CloudScope must match them to 1e-5. This also checks that the
  two `pixel_to_ray` implementations (OpenCV in C++, NumPy in STRATIA) agree on an equidistant fisheye.
- Desktop (`AiController`): the ray map is recomputed when the projection changes or the Sun moved by more than
  0.05°, otherwise reused frame after frame; every submitted frame carries the current geometry.

## Work log
1. Geometry module and the controller's caching.
2. STRATIA P096 follow-up: ray-map reference vectors; the nominal B0268 camera model copied as a fixture.
3. Tests `tests/unit/test_geometry_inputs.cpp`: the full-frame affine; the Sun-aligned frame and the meta vector;
   both reference cases match to 2e-5 with the same number of valid patches (1,024 and 925); unknown inputs give
   zeros; a mask hiding half of the sensor invalidates half of the patches; a horizon limit removes the lowest
   directions of a tilted camera; all directions are unit vectors.

## Verification
- `ctest -C Debug -R "unit\.(the full-frame|the Sun-aligned|the ray map|unknown geometry)"`: 4 cases pass; the
  largest difference to STRATIA's vectors is below 2e-5 (six-decimal vectors).
- `ui.the main window runs the dummy model on preview frames` (P068) checks that a window without a calibration
  file sends `geometry_valid = false`; the Gate F runs with the nominal B0268 calibration send valid geometry
  (`geometry_valid` column of the logged results).

## Exit criteria
- [x] Ray map matches STRATIA's generator on the same inputs (cross-repo test).

## Safety & failure-mode notes
A wrong pose or calibration does not break inference, it degrades it; the result's `geometry_valid` says whether
geometry was supplied, and the UI shows the calibration state through the overlays. The Sun comes from the host
clock and the configured site: a wrong site gives a wrong Sun azimuth, which only rotates the ray map's x/y
channels (the zenith angle is unaffected).

## Deviations & next phase
The camera mask (fixed structures) is supported but not configured in the desktop yet: the B0268 looks at open
sky. Next: P068 — AI overlays.
