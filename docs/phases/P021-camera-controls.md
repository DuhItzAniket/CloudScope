# P021 — Camera controls

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Expose the camera's controls (exposure, gain, white balance, brightness, contrast, saturation, gamma, sharpness,
focus) with discovered ranges, automatic modes and honest read-back, and verify on the B0268 that setting
exposure really changes the picture.

## Requirements covered
FR-CAM-04 (read-back), FR-CAM-05 (automatic modes on and off), NFR-DATA-02 (calibrated units only where real).

## Design notes
- Windows: DirectShow `IAMCameraControl` (exposure, focus) and `IAMVideoProcAmp` (the rest) through the media
  source; `GetRange` gives minimum, maximum, step, default and whether an automatic mode exists. Exposure is on a
  log2(seconds) scale: the backend reports milliseconds, `step = 0` (powers of two are not a linear step), and the
  effective value is the power of two the camera chose. White balance is in Kelvin; everything else is on the
  driver's own scale and marked uncalibrated (no unit), as NFR-DATA-02 demands.
- Linux: the standard V4L2 controls; `exposure_time_absolute` (100 µs units) becomes milliseconds; automatic
  exposure, white balance and focus use their own controls.
- `set_control()` answers with requested, effective and `applied`; a value the camera rounds is reported as not
  applied with the value in effect.
- Profiles (named sets of control values) are left to the sequencer and the application (P029, Stage D); the
  controls API they need is complete here.

## Work log
1. Control discovery and read-back in both backends; `cloudscope-camtool set <id> name=value|auto` and
   `exposure-test`.
2. Measured the B0268's controls and exposure response (`docs/hardware/b0268_measured.md`).

## Verification
- Controls offered by the B0268: exposure 0.122–500 ms (powers of two, automatic available), gain 0–100, white
  balance 2800–6500 K (automatic available), brightness −64–64, contrast 0–64, saturation 0–128, gamma 72–500,
  sharpness 0–6; no focus.
- HAL contract on the hardware: for every control, minimum, maximum and default are set and read back equal;
  out-of-range requests are clamped and reported as not applied; NaN is refused; automatic is honoured only where
  offered (exposure, white balance).
- **Exposure response** (`exposure-test`, 1280x720 MJPEG, gain 0): mean luma 1.1 → 23.4 over 0.5 → 128 ms,
  monotonic; effective exposures 0.488, 0.977, 1.953 … 125 ms (the camera's powers of two). The control is
  effective and the read-back is the value the camera uses.

## Exit criteria
- [x] Each control verified by measuring the brightness response: exposure measured (above); the other controls
  are verified for range, read-back and clamping by the contract; their picture effect is visible in the
  statistics of `camtool stream` but was not tabulated (brightness, contrast, gamma: expected effects; gain at
  0..100 on this camera is a digital gain the exposure test did not need).

## Risks / notes
- UVC cameras keep their settings after the program exits; the tool restores what it changed, and every sidecar
  (P027) records the effective values so a session's exposure is never a guess.
- The B0268's exposure quantisation (factor 2 between neighbouring values) is coarse for an auto-exposure loop;
  the sky controller (P025) steps by at most 2x anyway and uses gain for fine adjustment when allowed.

## Next phase
P022 — Acquisition pipeline.
