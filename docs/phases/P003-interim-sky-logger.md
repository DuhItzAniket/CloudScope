# P003 — Interim Sky Logger

Status: PARTIAL     Date: 2026-10-04     Commit: (this commit)

## Objective
Give the project a reliable way to start collecting Arducam B0268 sky images for STRATIA immediately: fixed-interval capture, UTC timestamps, a JSON sidecar per frame, exposure control, disk safety, on laptops and on a Raspberry Pi 5.

## Requirements covered
Pre-SRS. Feeds future FR groups "capture sequencer" (P029), "recording" (P027) and "dataset export" (P091).

## Design notes
- Python 3 + OpenCV, in `tools/sky_logger/` (package `sky_logger`, run with `python -m sky_logger`). Throwaway by design: replaced by the C++ capture sequencer in Stage C, but its sidecar schema (`cloudscope.sky_logger.frame/1`) is what STRATIA P019 ingests.
- **Honest controls:** every requested setting is recorded as `{requested, accepted, readback}`; nothing is assumed to have worked.
- **Drift-free schedule:** a fixed-rate deadline (`next_deadline`); late slots are skipped and counted, never captured in a burst.
- **Atomic writes:** `.part` file + `fsync` + rename for images and sidecars.
- **Solar position:** NOAA/Meeus equations implemented locally (no runtime dependency) and tested against pvlib's NREL SPA.
- Default backend: DirectShow on Windows (exposes UVC controls and the driver dialog), V4L2 on Linux.

## Work log
1. Implemented `solar.py`, `stats.py`, `storage.py`, `camera.py`, `cli.py` (`list`, `probe`, `run`), `README.md`, `sky-logger.service` (systemd), requirements files.
2. Wrote 28 unit tests (`tests/`), including a fake camera that reproduces the hardware quirks found below.
3. Tested on the only camera attached to the development laptop (the integrated Lenovo camera; the B0268 was not connected). Four real defects were found on hardware and fixed:

| # | Defect found on real hardware | Fix |
|---|---|---|
| 1 | Forcing `MJPG` by default: the driver "accepted" it, then delivered no frames | Default is now "keep driver format"; after configuration the camera is test-grabbed, and a format that yields no frames triggers a reopen with the driver default, recorded as `fallback` |
| 2 | DirectShow `grab()` faster than the frame rate returns success without a new frame, so `retrieve()` fails | Flush and capture with `read()`, which waits for a real frame; regression test added |
| 3 | A 2-point exposure test called a *falling* brightness "working", and `-1` read-backs were turned into "0.5 s" | Exposure test now sweeps ≥3 values, requires monotonic rise and a reproducible repeat; seconds are only computed when the logger set the exposure and the driver accepted it |
| 4 | **The exposure sweep left the laptop camera stuck at manual exposure -10** (UVC drivers keep settings after exit) | The test now restores the original value and then re-enables auto-exposure in that order (on DirectShow, writing a value re-enters manual mode); `--auto-exposure auto` with `--exposure` is rejected. The laptop camera was restored and verified (brightness re-adapts from 118 to 135) |

## Verification
- **Unit tests:** `python -m pytest tests` → `28 passed`.
- **Solar accuracy vs pvlib NREL SPA** (2,000 random times 2000–2050, latitudes ±66°): elevation error max 0.0165°, mean 0.0032°; azimuth on-sky error max 0.0148°, mean 0.0028°. Sanity: Bengaluru, 2026-10-04 12:08 IST → elevation 72.65°, matching a hand calculation (~72.5°).
- **Camera probe (integrated camera, DirectShow):** index 0 is a near-black stream without exposure control (likely the IR sensor); index 1 is the colour camera. Exposure sweep on index 1: mean luma 0.0 → 0.0 → 5.4 → 57.6 for −10/−8/−6/−4, verdict "effective"; afterwards a fresh open reads exposure −6 and auto-exposure re-adapts. MSMF could not open the camera on this machine.
- **End-to-end runs** (camera 1, output in a scratch folder, deleted afterwards; images were not viewed):

| Run | Settings | Result |
|---|---|---|
| A | 6 frames, 3 s interval, Bengaluru lat/lon | 6 images + 6 sidecars; SHA-256/size/name mismatches 0; `.part` leftovers 0; intervals 2.993–3.041 s; `capture_log.csv` 6 rows; session closed with `count reached`; sun elevation 39.77° at 15:18 IST (hand check ≈ 40°) |
| B | `--min-sun-elevation 85`, 7 s | 0 captures, 4 night skips, "pausing captures" logged |
| C | `--min-free-gb 100000` | stopped immediately, "disk guard", exit code 3 |

## Exit criteria
- [ ] **24-hour unattended run** — not done. The B0268 is not connected, and a 24-hour run with the indoor laptop camera would not be meaningful (and would record the room). → Do with the B0268 at the first opportunity (see below).
- [x] **Sidecars valid** — schema fields present, checksums verified, CSV and session records complete (run A).
- [ ] **Runs on Raspberry Pi 5** — not tested (no Pi available). Code paths for V4L2 and the systemd unit are written but unverified.
- [ ] **STRATIA P019 ingests the output** — STRATIA has not started (waiting for DINOv3 access); the schema is documented in `tools/sky_logger/README.md` for that phase.

## Safety & failure-mode notes
- UVC settings persist across programs. Any tool that changes camera settings must restore them or record them; the logger records read-backs in every sidecar.
- The time source is the host clock (stated in each sidecar); NTP/GPS discipline arrives with P053. Keep Windows time sync enabled.
- No Sun protection exists yet (fixed camera, no motion). Do not point a camera at the Sun for long periods without a filter.

## Deviations & next phase
- Status PARTIAL because three exit criteria need hardware or STRATIA. **Completion task (P003-acceptance):** connect the B0268, run `list`, `probe --fourcc MJPG --width 4656 --height 3496`, `probe --test-exposure`, then a 24-hour `run` at zenith; append results to this document and change the status to DONE.
- Next: **P004 — Use cases & personas**.
