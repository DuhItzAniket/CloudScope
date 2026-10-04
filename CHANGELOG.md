# Changelog

All notable changes, grouped by phase. Versions follow [Semantic Versioning](https://semver.org/); the system reboot targets v1.0.0 at P100.

## [Unreleased]

### P003 — Interim Sky Logger (2026-10-04, PARTIAL)
- Added `tools/sky_logger`: fixed-interval sky capture with UTC timestamps, per-frame JSON sidecars (settings read-back, image statistics, Sun position), drift-free scheduling, atomic writes, disk guard, night pause, camera reconnect, systemd unit for Raspberry Pi.
- 28 unit tests; solar position within 0.02 deg of NREL SPA.
- Fixed four defects found on real hardware (MJPG with no frames, DirectShow grab/retrieve quirk, misleading exposure test, camera settings left in manual mode).

### P002 — Docs & phase protocol (2026-10-04)
- Added `docs/PLAN.md` (100-phase plan), phase and ADR templates, `CONTRIBUTING.md`, `PROJECT_STATE.md`, this changelog.
- Added Apache-2.0 `LICENSE` (default; to be confirmed in ADR-010, P008).
- Added `.gitattributes` to normalise line endings (LF in the repository, CRLF for Windows scripts).

### P001 — Legacy freeze (2026-10-04)
- Tagged the AI-Day prototype `v0.1-aiday`.
- Moved all prototype code, models, reports and history into `legacy/` (history preserved).
- New root `README.md` and `.gitignore` for the system layout.

## [0.1.0-aiday] — 2026-09-28
Prototype for the college AI Day: CCSN cloud classifier (MobileNetV3-Large), LenghuSky-8 segmenter, Qt6/QML desktop app with ONNX Runtime. See `legacy/history/`.
