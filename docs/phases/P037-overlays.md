# P037 — Overlays

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Reticle and grid, and with a calibration and a pose the sky geometry on the picture: altitude circles, compass
points, the Sun's position and the keep-out zone (FR-DSP-06, FR-DSP-07).

## Requirements covered
FR-DSP-06, FR-DSP-07; FR-SAF-01 (the keep-out cone is drawn with the configured radius).

## Design notes
- `SkyProjection` (`overlay.hpp`): the P031 camera model (`ray_to_pixel`, `pixel_to_ray`) and the orientation of
  `rotation.hpp` (`camera_orientation` for the declared azimuth and elevation, then the roll about the optical
  axis) map sky directions to pixels and back. Directions behind the camera are not drawn.
- Altitude circles at 0°, 30°, 60° as polylines broken where they leave the view; the cardinal points at 10°
  elevation; the Sun from `sun_position()` with the site and the clock (refreshed every 30 s); the keep-out ring as
  the set of directions at the configured angular distance from the Sun.
- `paint_overlays()` reports what it drew; sky overlays without a calibration are not drawn and the main window
  raises one notification saying why (no silent pretence).
- The pose comes from `desktop.pointing` (declared by the operator) until a mount or IMU reports it (Stage E).

## Work log
1. `overlay.cpp`; actions and shortcuts for every overlay (R, G, A, C, S, K); the calibration file from
   `desktop.calibration_file` (Preferences > Paths).

## Verification
- `ui.a zenith camera maps the zenith to the principal point and elevation to radius`: for an ideal equidistant
  fisheye (f = 300 px) the pixel radius equals f · (90° − elevation) to 1e-6 px for every azimuth.
- `ui.pixels and sky directions invert each other` (pose with azimuth 35°, elevation 50°, roll 12°).
- `ui.the Sun marker agrees with the solar position to a millionth of a degree`: the marker's back-projected
  direction equals `sun_position()` for Bengaluru at 2026-10-09T06:30Z within 1e-6°; every point of the keep-out
  ring lies exactly 15° from the Sun.
- `ui.altitude circles and compass marks lie where the model puts them`; `ui.painting draws what is enabled and
  reports it honestly`.

## Exit criteria
- [x] Overlay accuracy checked against sun position (test above).

## Safety & failure-mode notes
- The keep-out ring is a display of the planner's rule, not the enforcement (P060 does that in the host and the
  firmware).

## Deviations & next phase
- None. Next: P038 — Image tools.
