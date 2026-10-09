# P025 — Sky auto-exposure and bracketing

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
An exposure controller that protects cloud highlights instead of averaging the whole picture, plus exposure
brackets and an HDR merge, so that a sky camera keeps cloud texture when the Sun is in the frame.

## Requirements covered
FR-CAM-09 (sky-specific automatic exposure; bracketing with HDR merge).

## Design notes
`core/include/cloudscope/capture/exposure.hpp`:
- Metering: the 99th percentile of luma **outside the Sun's disc** (the Sun found by P024's `SunBlob`, grown by
  `sun_margin` = 2.5 radii) is driven to `target_level` 230 within a dead band of ±8 levels; the step is limited
  to a factor `max_step_ratio` = 2 per update, so the loop cannot oscillate on a stepwise camera.
- `SkyExposureController::update(image, exposure_ms, gain)` returns an `ExposureDecision` (new exposure, gain,
  the metered level, whether anything changed, and the reason in words). Exposure moves first; gain only when the
  exposure is at its limit.
- `bracket_exposures(base, stops, min, max)` gives the exposures of a bracket in photographic stops, clipped to
  the camera's range; `merge_mertens()` fuses a bracket with exposure-fusion weights (contrast, saturation,
  well-exposedness) through a Laplacian pyramid, as OpenCV's Mertens does but without the `photo` module.
- `cloudscope-camtool ae-test <id> [--mode] [--seconds]` runs the camera's own automatic exposure and then the
  sky controller on the same scene and prints both results.

## Work log
1. Module and tests (`tests/unit/test_exposure_calibration.cpp`).
2. `ae-test` on the Arducam B0268 (`build/p025_aetest.log`): see verification.

## Verification
- Synthetic scenes (320x240, cloud tops 200, sky 90, a clipped Sun): the meter ignores the Sun and finds the cloud
  tops; the controller raises exposure for a dark scene and lowers it for a clipped one, never more than a factor
  of two per step, and holds still inside the dead band; brackets are symmetric in stops and clipped to the range;
  the Mertens merge of a 3-stop bracket of a synthetic sky has fewer clipped pixels than the brightest frame and
  more detail (standard deviation) in the dark cloud base than the darkest frame.
- On the simulated sky camera the sky controller reaches a clipped fraction outside the Sun no higher than the
  camera's own automatic exposure.
- **B0268 run (2026-10-09):** the camera saw a black scene (lens covered; luma 0.0 in both halves). Both methods
  therefore reported 0 % clipped, and the comparison is not a measurement of the sky behaviour. What the run did
  show: the controller reacted correctly to darkness, opening from 15.6 ms (the camera's auto value) to the mode's
  maximum power-of-two step 31.25 ms within one update and holding there; the camera's exposure control follows
  the request (P021 measured the response curve). **The daylight comparison is an open item** for the owner's
  first sky session: run `cloudscope-camtool ae-test uvc:0c45:636d:1 --mode 1920x1080@30/MJPEG --seconds 30` with
  the Sun in the field and record the two clipped fractions here.

## Exit criteria
- [ ] On a sky with the Sun in frame, the sky controller clips fewer non-Sun pixels than the camera's automatic
  exposure: shown on the simulator; **not yet shown on the B0268** (dark scene during the run; see above).

## Risks / notes
- The B0268 exposes in powers of two (log2 seconds), so the controller's dead band of ±8 levels is sometimes
  wider than one camera step; a sky can sit between two steps. Gain (0–100, uncalibrated) covers the gap.
- HDR merging at 16 MP takes seconds in Debug; it is meant for stills, not the live stream.

## Next phase
P026 — Calibration frames.
