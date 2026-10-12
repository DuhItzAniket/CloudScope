# P035 — Camera control panel

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Every control the camera lists, with its real range and step, automatic toggles where the device offers them,
the value read back after each request (marked when the device did not take it), the modes, and named profiles.

## Requirements covered
FR-CAM-03 (modes listed and selectable), FR-CAM-04 (read-back and marking), FR-CAM-05 (manual / automatic
exposure, gain, white balance; controls the camera lacks are not shown), FR-CAM-07 (profiles).

## Design notes
- `CameraPanel` (`camera_panel.hpp`) builds one row per `ControlInfo`: slider with the device's step count, spin box
  with the unit (or "not calibrated" in the tooltip), *Auto* box if `supports_auto`, and the read-back label from
  `ControlState`: `applied == false` shows `device set X (asked Y)` in bold (FR-CAM-04). Nothing is shown that the
  capabilities do not list (NFR-USE-01).
- Modes come from the capabilities; a device in a mode it did not list is shown marked "(not listed)".
- Profiles (`camera_profiles.hpp`): JSON file `camera-profiles.json` next to `config.toml`, schema
  `cloudscope.camera_profiles/1`; a profile holds a mode and control values; applying one goes through the service
  so that every read-back reaches the rows, and controls the camera lacks are reported.
- The device list marks simulated cameras.

## Work log
1. `camera_panel.cpp`, `camera_profiles.cpp` (mode text `1920x1080@30/MJPEG` parsing shared with `--mode`).
2. Profiles are listed, saved, applied and deleted from the panel; `ProfileStore` validates the file.

## Verification
- `ui.every control and the mode round-trip to the simulated camera`: for every control, a value set in the row is
  read back from the device and equals the device's own `control()`; the *Auto* box sets and clears automatic
  mode on the device; a mode chosen in the box becomes the device's mode.
- `ui.a request the device does not take is marked in the read-back column`: a value beyond the range is clamped
  by the device, `applied` is false and the row says "device set".
- `ui.profiles are saved, listed, applied and deleted` and `ui.profile files are validated and modes are parsed`.
- Manual with the B0268: exposure, gain, white balance, brightness, contrast, saturation, sharpness rows with the
  driver's ranges; automatic exposure toggles; profiles saved and reapplied.

## Exit criteria
- [x] Every control round-trips to device (test above).

## Safety & failure-mode notes
- A control the device refuses leaves the row at the device's value and raises a notification; nothing is
  pretended (quality bar in `CONTRIBUTING.md`).

## Deviations & next phase
- None. Next: P036 — Histogram, scopes & stretch.
