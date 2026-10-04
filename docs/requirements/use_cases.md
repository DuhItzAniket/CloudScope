# CloudScope — Personas, Deployments and Use Cases

Version 1.0 · Phase P004 · 2026-10-04

This document says **who** uses CloudScope, **where** it runs, and **what** they need to do. The SRS (`SRS.md`, P005) turns each use case into testable requirements.

---

## 1. Personas

| ID | Persona | Goals | Context | What they must never experience |
|---|---|---|---|---|
| P1 | **Researcher / data collector** (the project owner building STRATIA datasets) | Weeks of consistent, calibrated, well-labelled sky data; exact UTC times and camera settings; easy export to STRATIA | Rooftop camera in Bengaluru; laptop or Pi 5; checks in remotely | Silent gaps, wrong timestamps, unrecorded setting changes, corrupted files |
| P2 | **Observer / operator** | Look at the sky live, point the camera, tune exposure, capture images and time-lapses, see what STRATIA thinks | At the device or on the same network; uses keyboard, mouse or gamepad | A cluttered or fake UI, controls that silently don't apply, the camera driven into the Sun |
| P3 | **Remote viewer** (teammate, faculty mentor, audience at a demo) | Watch the live view, recent captures and cloud analysis from a phone or browser | Anywhere; no installation; may have read-only access | Being able to move hardware without permission, slow or broken streams |
| P4 | **Developer / integrator** | Add a camera, sensor or controller driver; update firmware; plug in a new STRATIA model; script missions through the API | Laptop with the source tree; simulator when hardware is absent | Undocumented interfaces, behaviour that differs between simulator and hardware |

## 2. Deployment configurations

| ID | Configuration | Host | Controller | Typical use |
|---|---|---|---|---|
| D1 | **Fixed camera, laptop** | Windows/Linux laptop | none | Data collection, live viewing, AI analysis (this is the current state) |
| D2 | **Pan-tilt, laptop** | Laptop | ESP32 over USB serial or Wi-Fi | Manual pointing, sky surveys, development |
| D3 | **Headless field station** | Raspberry Pi 5 running the daemon | ESP32, or Pi GPIO/I2C directly | Long unattended campaigns, remote access |
| D4 | **Minimal pan-tilt** | Laptop or Pi 5 | Arduino Uno (lite protocol: servos + raw IMU) | Low-cost builds; reduced features (no on-board fusion, no Wi-Fi) |

In every configuration the **host** runs CloudScope; a microcontroller is only a peripheral. The same daemon and API run in all four.

## 3. Use cases

Format: actor · preconditions · main flow · alternatives/exceptions · result. "→ FR" lists the requirement groups in the SRS.

### UC-01 Connect and configure a camera
- **Actor:** P2, P1 · **Pre:** camera plugged in.
- **Main:** CloudScope lists cameras with stable names → user selects one → available modes (resolution, pixel format, frame rate) and controls are discovered → user picks a mode and adjusts exposure, gain and white balance → settings are applied, **read back**, and shown → user saves them as a named profile.
- **Alt:** a requested mode delivers no frames → CloudScope falls back and tells the user. A control is not supported → it is shown as unavailable, not as a dead slider.
- **Result:** live frames in the chosen mode; the effective settings are known. → FR-CAM

### UC-02 View and inspect the live sky
- **Actor:** P2 · **Pre:** UC-01.
- **Main:** live view with zoom/pan/1:1 → histogram and clipping indicators → focus aid → overlays (grid, altitude circles, compass, Sun position and keep-out zone) → pixel and region statistics.
- **Result:** the operator can judge exposure and framing objectively. → FR-DSP

### UC-03 Capture images
- **Actor:** P2, P1 · **Main:** choose format (JPEG, PNG/TIFF-16, FITS, SER, MP4) → single shot, burst or bracket → files are written atomically with a metadata sidecar and catalogued in the session.
- **Exception:** disk nearly full → capture refused with a clear message. → FR-REC

### UC-04 Run an interval capture / time-lapse
- **Actor:** P1 · **Main:** set interval, start/stop condition (count, duration, time window, Sun elevation) → sequencer runs drift-free → progress and next-capture countdown shown → time-lapse video can be assembled afterwards.
- **Exception:** capture slower than interval → slot skipped and logged; camera lost → reconnect with backoff. → FR-SEQ

### UC-05 Calibrate the camera
- **Actor:** P1, P4 · **Main:** guided checkerboard capture → intrinsic fit (fisheye or pinhole model) → reprojection error reported → calibration saved in a format STRATIA can read.
- **Result:** pixel ↔ sky-direction mapping available to overlays, missions and STRATIA. → FR-CAL

### UC-06 Connect a controller and sensors
- **Actor:** P2, P4 · **Pre:** ESP32/Uno connected, or Pi GPIO wiring present.
- **Main:** CloudScope scans ports → handshake reports the controller's capabilities (axes, IMU type, GPS, sensors) → UI and planner enable only what exists → telemetry starts streaming.
- **Alt:** firmware protocol version too old → user is told to update firmware; nothing is driven. → FR-CTL, FR-FW

### UC-07 Point the camera manually
- **Actor:** P2 · **Pre:** UC-06, mount homed.
- **Main:** joystick widget, keyboard or gamepad moves pan/tilt → or "go to az/el" → motion follows smooth profiles within soft limits → the sky-dome view shows where the camera looks.
- **Exception:** target inside the Sun keep-out zone → command refused with an explanation; e-stop stops all motion. → FR-CTL, FR-SAF

### UC-08 Calibrate the mount
- **Actor:** P1, P4 · **Main:** homing → IMU–camera alignment → pointing model fitted from the Sun's position at safe exposures and from landmarks → residual pointing error reported.
- **Result:** commanded az/el and image overlays agree to a stated accuracy. → FR-CAL

### UC-09 Run an autonomous mission
- **Actor:** P1 · **Pre:** calibrated camera (and mount, if present).
- **Main:** select or write a mission (zenith cloud-base-height sampling, all-sky survey, adaptive time-lapse, cloud tracking, event-triggered bursts) → validate → dry-run on the simulator → start → mission runs on schedule, pausing at night or in bad conditions → operator can always override.
- **Exception:** fault (camera loss, controller loss, disk) → mission pauses, mount parks, operator is notified. → FR-MOD, FR-MIS, FR-SAF

### UC-10 See STRATIA's analysis live
- **Actor:** P2, P3 · **Pre:** STRATIA model installed.
- **Main:** frames are analysed at a configurable rate → overlays show sky/cloud/glare masks, cloud layers, genus with confidence, cloud cover, cloud-base height with an uncertainty interval → results are logged and plotted over time.
- **Exception:** input out of the model's domain (night, glare, unknown camera) → results are marked "uncertain", not displayed as facts. → FR-AI

### UC-11 Monitor remotely
- **Actor:** P3 · **Pre:** remote access enabled by the owner.
- **Main:** open the web dashboard → sign in → live stream, latest captures, telemetry, AI timeline → read-only by default. → FR-REM, FR-SEC

### UC-12 Control remotely
- **Actor:** P2 (remote) · **Main:** sign in with operator rights → take the control lock → same controls as locally → release the lock.
- **Exception:** network drops → controller heartbeat keeps hardware safe; lock times out. → FR-REM, FR-SEC, FR-SAF

### UC-13 Browse sessions and export a dataset
- **Actor:** P1 · **Main:** browse sessions by date, filter by metadata (Sun elevation, exposure, AI results) → select → export in STRATIA's ingest format with checksums. → FR-DAT

### UC-14 Label captured images
- **Actor:** P1 · **Main:** export a selection to Label Studio/CVAT → annotate → import labels back into the catalogue. → FR-DAT

### UC-15 Survive faults safely (system use case)
- **Actor:** system · **Triggers:** camera unplugged, controller reset, link loss, disk full, power loss, Sun approaching the optical axis, overheating.
- **Main:** detect → move to a safe state (stop motion, park, pause capture) → log → notify → recover automatically when possible → resume missions where safe. → FR-SAF, NFR-REL

### UC-16 Update models and firmware
- **Actor:** P4, P1 · **Main:** install a STRATIA model (checksum and contract version verified) or flash new firmware (version handshake) → roll back if needed. → FR-AI, FR-FW

### UC-17 Deploy a headless field station
- **Actor:** P1, P4 · **Main:** install the package on a Pi 5 → configure site, storage and camera → enable the service → station starts on boot and is reachable through the dashboard. → FR-PLT, NFR-REL

## 4. Use-case-to-plan map

| Use case | Delivered mainly in |
|---|---|
| UC-01, UC-02, UC-03, UC-04, UC-05 | Stage C (P019–P032), Stage D (P033–P046) |
| UC-06, UC-07, UC-08, UC-15 (hardware) | Stage E (P047–P064) |
| UC-10, UC-16 (models) | Stage F (P065–P072) |
| UC-09 | Stage G (P073–P082) |
| UC-11, UC-12, UC-17 | Stage H (P083–P090), packaging P098 |
| UC-13, UC-14 | Stage I (P091–P093) |

The interim sky logger (P003) already covers a minimal UC-04 for configuration D1.
