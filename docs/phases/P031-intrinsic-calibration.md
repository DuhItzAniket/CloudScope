# P031 — Intrinsic calibration tool

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Calibrate a camera's intrinsics from a checkerboard: detect the board, help the operator collect views that cover
the whole image, fit OpenCV's pinhole or fisheye model, and write the camera-model file STRATIA reads.

## Requirements covered
Groundwork for FR-DSP-07 (overlays need pixel ↔ direction), FR-REC-12 (WCS with a calibration), the metadata
provider's ray map (P067) and STRATIA's camera models (STRATIA P043).

## Design notes
`core/include/cloudscope/calibration/intrinsics.hpp`:
- `detect_checkerboard(image, board)`: OpenCV's `findChessboardCornersSB` (exhaustive, accurate) with the classic
  detector plus `cornerSubPix` as fallback; inner corners row by row.
- `CaptureAssistant`: the image split into a 4×4 grid; a view is kept when its corners cover a cell no earlier view
  covered, so the operator is pushed to the edges; `coverage()` says how much of the image is covered.
- `fit_intrinsics(views, board, model)`: `cv::fisheye::calibrate` (equidistant model, k1–k4, skew fixed, extrinsics
  recomputed) or `cv::calibrateCamera` (k1 k2 p1 p2 k3), both from OpenCV's own initial estimate. A guessed focal
  length far from the truth sent the fisheye fit into a wrong minimum (rms 33 px) in the first version; the
  initial guess was removed.
- `reprojection_error(model, view)`: the board's pose from `solvePnP` with the planar IPPE solver, then the rms of
  projected minus detected corners.
- `pixel_to_ray` / `ray_to_pixel`: unit direction in the camera frame (x right, y down, z forward) and back.
- The camera-model file, schema `cloudscope.camera_model/1` (`core/resources/camera_model.schema.json`): `model`
  (`opencv_fisheye` or `opencv_pinhole`), the image size the parameters refer to, `fx fy cx cy`, the distortion
  vector, the fit's rms and view count, the board, camera id and name, calibration id, time and software. This is
  the format STRATIA P043 will load for the B0268 ("OpenCV fisheye/pinhole (B0268)" in STRATIA's plan); the
  distortion vector's meaning is fixed by the model name.
- `cloudscope-camtool calibrate <id> --out model.json [--board 9x6 --square 25 --lens fisheye --views 15]` is the
  capture assistant from the command line (one look per second, kept views announced with the coverage); `--from
  FOLDER` fits pictures on disk.

## Work log
1. Module, schema, tests (`tests/unit/test_intrinsics.cpp`), camtool command, OpenCV `calib3d` added to the
   dependency list (vcpkg feature already present; Debian's `libopencv-dev` includes it).
2. Fisheye fit fixed (no intrinsic guess), IPPE for the pose, detector tolerance set from the rendered board.

## Verification
Synthetic data, since the owner has not printed a board yet:
- Fisheye: a 640x480 model (f = 300/302, centre 325/236, k = −0.05 0.01 −0.002 0.0005) seen in 14 poses, corners
  with 0.05 px noise: rms 0.068 px, f recovered as 300.13/302.08, centre 325.03/236.21, k1 within 0.02; every view's reprojection error below 0.5 px; pixel → ray → pixel round
  trips within 0.01 px.
- Pinhole with distortion (f = 520/518, k1 = −0.20): recovered within 1 % and 2 px; fewer than three views or mixed
  sizes are refused.
- Detector: a rendered 9×6 board under a perspective warp: all 54 corners found within 1 px of the known positions
  (the warp's interpolation and blur move the rendered edges themselves); colour input accepted; a plain picture is
  `NotFound`.
- Assistant: a repeated view is rejected ("move the board"), another image size is rejected, views that cover new
  cells are kept and coverage grows past 50 %.
- Camera-model file: validates against its schema, round-trips every field, refuses five coefficients on a
  fisheye, unknown model names and missing parts.

## Exit criteria
- [x] Reprojection error < 0.5 px on the synthetic fisheye and pinhole fits.
- [~] File loads in STRATIA P043: the format is defined here; the STRATIA side does not exist yet (P043 is a later
  STRATIA phase and will read this file).
- [ ] B0268 calibrated: needs a printed checkerboard (owner). Command ready: `cloudscope-camtool calibrate
  uvc:0c45:636d:1 --mode 1920x1080@30/MJPEG --lens fisheye --out docs/hardware/b0268.camera.json`.

## Risks / notes
- The B0268's lens is a wide fisheye (about 180°); OpenCV's equidistant model holds to roughly 160° of field, and
  corners near the rim are hard to detect. A model that covers the rim (OCamCalib-style, as Eye2Sky uses) may be
  needed later; the file format carries the model name for that reason.
- The calibration is for one mode's image size; other sizes of the same sensor scale fx, fy, cx, cy by the size
  ratio only when the sensor is not cropped. The file states the size it refers to.

## Next phase
P032 — Camera soak test.
