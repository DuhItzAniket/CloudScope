# CloudScope — Software Requirements Specification (SRS)

Version 1.2 · Phases P005, P006, P008 · 2026-10-04 · Source: `use_cases.md` (P004), `docs/PLAN.md`, `docs/research/P006_competitive_analysis.md`

**Revision history:** v1.0 (P005) initial, 138 requirements. v1.1 (P006) adds 10 requirements and tightens FR-REC-02 after the competitive analysis (FR-CAM-12, FR-DSP-11, FR-REC-10/11/12, FR-SEQ-06, FR-CTL-12/13, FR-REM-09/10). v1.2 (P008) changes FR-PLT-01 to Raspberry Pi OS on Debian 13 "Trixie" with Qt ≥ 6.8 (ADR-001): Bookworm ships Qt 6.4, below the minimum.

## 1. Introduction

### 1.1 Purpose and scope
This SRS defines what CloudScope v1.0 must do (functional requirements, FR) and how well (non-functional requirements, NFR). Every requirement has an ID, a priority, a verification method and the plan phase that delivers it. The acceptance test suite (P097) has one test per requirement, named `AT-<requirement ID>`.

CloudScope is the complete sky-observation system: camera application, hardware control, operating modes, remote access, data management and STRATIA inference. STRATIA itself (model training) is out of scope; only its runtime interface is in scope.

### 1.2 Definitions
| Term | Meaning |
|---|---|
| Host | The computer running CloudScope (laptop or Raspberry Pi 5) |
| Controller | A microcontroller (ESP32, Arduino Uno) or the Pi's own GPIO/I2C, driving actuators and reading sensors |
| Daemon | `cloudscoped`, the headless service that owns devices and exposes the API |
| Client | Desktop application or web dashboard connected to the daemon |
| Mount | The pan-tilt mechanism and its kinematic model |
| Keep-out zone | Region of the sky (around the Sun) the camera's optical axis must not enter |
| Sidecar | JSON metadata file written next to each captured image |
| Mission | Declarative description of an autonomous task (steps, conditions, schedule) |
| STRATIA contract | Versioned input/output specification of the STRATIA ONNX model (`stratia-contract`) |
| CSDP | CloudScope Device Protocol between host and controller |

### 1.3 Conventions
- **IDs:** `FR-<GROUP>-<NN>` and `NFR-<GROUP>-<NN>`. IDs are never reused; removed requirements are marked *withdrawn*.
- **Priority (MoSCoW):** **M** must (v1.0 does not ship without it), **S** should, **C** could.
- **Verification:** **T** automated test, **D** demonstration, **I** inspection/review, **A** analysis/measurement.
- **Phase:** the plan phase that implements it (`docs/PLAN.md`).
- "The system" means CloudScope (host software plus firmware).

---

## 2. Functional requirements

### 2.1 Camera (FR-CAM) — UC-01
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-CAM-01 | The system shall enumerate connected cameras on Windows (Media Foundation/DirectShow) and Linux (V4L2) and identify each with a name that stays stable across restarts. | M | T | P019 |
| FR-CAM-02 | The system shall detect camera connection and disconnection while running. | M | T | P019 |
| FR-CAM-03 | The system shall list each camera's supported resolutions, pixel formats and frame rates, and let the user select one. | M | T | P020 |
| FR-CAM-04 | After applying any mode or control, the system shall read the value back from the driver and display the effective value, marking settings the driver rejected or ignored. | M | T | P021 |
| FR-CAM-05 | The system shall expose manual and automatic exposure, gain and white balance when the camera supports them, and hide or disable controls it does not support. | M | T | P021 |
| FR-CAM-06 | If a selected mode delivers no frames, the system shall fall back to a working mode and inform the user. | M | T | P020 |
| FR-CAM-07 | The system shall save and load named camera profiles. | S | T | P021 |
| FR-CAM-08 | The system shall timestamp every frame with UTC wall-clock time (millisecond resolution) and a monotonic host time, and count dropped frames. | M | T | P022 |
| FR-CAM-09 | The system shall provide a sky-specific automatic exposure that protects cloud highlights, and exposure bracketing with HDR merge. | S | A | P025 |
| FR-CAM-10 | The system shall support dark-frame and flat-field correction. | C | A | P026 |
| FR-CAM-11 | The system shall support Raspberry Pi CSI cameras through libcamera. | C | D | P019 |
| FR-CAM-12 | The system shall expose raw sensor formats (RAW8/RAW16/MONO16), region of interest and binning when the camera or its SDK provides them. | S | T | P020 |

### 2.2 Display and analysis (FR-DSP) — UC-02
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-DSP-01 | The system shall show the live image with zoom, pan, fit-to-window and 1:1 pixel views, and display frame rate and dropped frames. | M | D | P034 |
| FR-DSP-02 | The system shall show a live histogram (linear and logarithmic) with clipping indicators. | M | T | P036 |
| FR-DSP-03 | The system shall show per-channel (RGB parade) statistics. | S | T | P036 |
| FR-DSP-04 | The system shall provide a focus aid based on a sharpness metric. | S | T | P038 |
| FR-DSP-05 | The system shall provide a pixel inspector and region-of-interest statistics. | S | T | P038 |
| FR-DSP-06 | The system shall draw reticle and grid overlays. | M | D | P037 |
| FR-DSP-07 | With a valid calibration and pose, the system shall overlay altitude circles, a compass, the Sun's position and the keep-out zone. | M | A | P037 |
| FR-DSP-08 | The desktop UI shall use dockable panels whose layout persists between sessions, and offer dark and red night-vision themes. | M | D | P033 |
| FR-DSP-09 | The system shall show device, mount, sensor, AI, remote-client and disk status in a status bar, and keep a searchable log console. | M | D | P042 |
| FR-DSP-10 | The system shall support keyboard shortcuts for all frequent actions and gamepad input for motion. | S | D | P043 |
| FR-DSP-11 | The system shall apply display stretch (manual and automatic) to the preview only, never to recorded data, and shall highlight over-exposed pixels. | M | T | P036 |

### 2.3 Recording and metadata (FR-REC) — UC-03
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-REC-01 | The system shall save frames as JPEG, PNG and 16-bit TIFF. | M | T | P027 |
| FR-REC-02 | The system shall save frames as FITS with standard header keywords: at least DATE-OBS (UTC, ms), TIMESYS, MJD-OBS, EXPTIME, GAIN, OBSGEO-B/L/H and SITELAT/SITELONG/SITEELEV (decimal degrees, east positive), CENTALT/CENTAZ, ROWORDER, SWCREATE and the calibration ID. | M | T | P027 |
| FR-REC-03 | The system shall record video as SER and, when FFmpeg is available, MP4/H.264. | S | T | P028 |
| FR-REC-04 | The system shall write a JSON sidecar for every saved frame containing capture time, camera settings read back from the driver, site, pointing (stating whether it is measured or declared), calibration ID, image statistics, Sun position and software versions. | M | T | P027 |
| FR-REC-05 | Every file shall be written atomically (temporary file, flush, rename) and its SHA-256 recorded. | M | T | P027 |
| FR-REC-06 | File names shall be sortable, contain the site ID and the UTC time to the millisecond, and follow a user-configurable template. | M | T | P029 |
| FR-REC-07 | The system shall refuse to start a capture, and stop a running one, when free space falls below a configurable threshold. | M | T | P029 |
| FR-REC-08 | The sidecar schema shall be versioned and backward compatible with the interim logger schema `cloudscope.sky_logger.frame/1`. | M | T | P027 |
| FR-REC-09 | The system shall assemble time-lapse videos from captured sequences. | C | D | P028 |
| FR-REC-10 | 16-bit TIFF files shall carry an AstroTIFF header (FITS-style keywords in the ImageDescription tag). | S | T | P027 |
| FR-REC-11 | The system shall produce keograms and star-trail images from captured sequences. | S | T | P028 |
| FR-REC-12 | For frames with a valid calibration, FITS files shall include zenithal-projection WCS keywords. | C | T | P027 |

### 2.4 Sequencer (FR-SEQ) — UC-04
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-SEQ-01 | The system shall capture single frames, bursts, exposure brackets and fixed-interval sequences. | M | T | P029 |
| FR-SEQ-02 | Interval sequences shall follow a fixed-rate schedule without cumulative drift; missed slots shall be skipped and counted, not captured late in a burst. | M | T | P029 |
| FR-SEQ-03 | Sequences shall stop on count, duration, end time, or a Sun-elevation condition, and pause/resume on Sun elevation. | M | T | P029 |
| FR-SEQ-04 | The system shall show sequence progress and the time to the next capture. | M | D | P039 |
| FR-SEQ-05 | After a camera failure the sequencer shall reopen the camera with exponential backoff and continue. | M | T | P029 |
| FR-SEQ-06 | The system shall switch between day and night capture profiles (exposure, gain, interval) at a configurable Sun elevation. | M | T | P029 |

### 2.5 Calibration (FR-CAL) — UC-05, UC-08
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-CAL-01 | The system shall guide checkerboard capture and fit camera intrinsics with fisheye and pinhole models, reporting reprojection error. | M | A | P031 |
| FR-CAL-02 | Calibrations shall be stored with an ID and validity period and exported in the format STRATIA reads. | M | T | P031 |
| FR-CAL-03 | The system shall home the mount and calibrate per-axis actuator offsets. | M | D | P057 |
| FR-CAL-04 | The system shall estimate the IMU-to-camera orientation and report its residual. | S | A | P058 |
| FR-CAL-05 | The system shall fit a pointing model from Sun positions (at safe exposures) and landmarks, and report pointing RMS error. | S | A | P059 |
| FR-CAL-06 | The system shall map any pixel to a sky direction (azimuth, elevation) and back using the active calibration and pose. | M | T | P031, P056 |

### 2.6 Control system and hardware abstraction (FR-CTL) — UC-06, UC-07
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-CTL-01 | All hardware shall be accessed through abstract interfaces (camera, mount, IMU, sensor, transport, inference) so that drivers can be added without changing other modules. | M | I | P017 |
| FR-CTL-02 | The system shall provide simulated camera, mount, IMU and GPS devices usable everywhere real ones are. | M | T | P018 |
| FR-CTL-03 | The system shall communicate with controllers over USB serial and Wi-Fi (TCP/UDP) using CSDP. | M | T | P047, P048 |
| FR-CTL-04 | The system shall discover controllers automatically and adapt the available features to the capabilities each controller reports. | M | T | P049 |
| FR-CTL-05 | The system shall drive pan and tilt axes through a PCA9685 PWM driver, direct PWM pins, or (later) stepper drivers. | M | T | P051, P055 |
| FR-CTL-06 | The system shall support the Raspberry Pi 5's own I2C/PWM without a microcontroller. | S | T | P055 |
| FR-CTL-07 | The system shall convert between actuator positions and azimuth/elevation using a kinematic model with mechanical offsets. | M | T | P056 |
| FR-CTL-08 | Motion shall follow acceleration-limited profiles within configurable soft limits. | M | T | P057 |
| FR-CTL-09 | The user shall be able to jog axes (on-screen joystick, keyboard, gamepad) and command a go-to az/el. | M | D | P061 |
| FR-CTL-10 | The system shall display mount orientation on a sky-dome widget showing the camera footprint, the Sun and the keep-out zone. | M | D | P061 |
| FR-CTL-11 | The system shall read and log IMU attitude, GPS time/position, RTC, temperature/humidity/pressure, rain and light sensors when present. | S | T | P052, P053, P062 |
| FR-CTL-12 | The system shall control ASCOM Alpaca devices (telescope/mount in alt-az, focuser, switch, ObservingConditions) through the HAL, including Alpaca discovery. | S | T | P055 |
| FR-CTL-13 | On Linux, the system shall use INDI devices through an INDI client backend. | C | T | P055 |

### 2.7 Firmware (FR-FW) — UC-06, UC-16
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-FW-01 | ESP32 firmware shall implement CSDP, servo/PWM output, IMU reading with on-board orientation fusion (or BNO08x quaternions), auxiliary sensors, a hardware watchdog and persistent configuration. | M | T | P050–P053 |
| FR-FW-02 | Arduino Uno firmware shall implement the CSDP-Lite subset: servos, raw MPU-6050 data, heartbeat, e-stop. | S | T | P054 |
| FR-FW-03 | Firmware shall report protocol version and capabilities in its handshake; the host shall refuse to drive firmware with an incompatible protocol version. | M | T | P049 |
| FR-FW-04 | ESP32 firmware shall support over-the-air updates. | C | D | P050 |
| FR-FW-05 | Firmware and host protocol code shall be generated from one shared schema. | M | I | P047 |

### 2.8 Safety (FR-SAF) — UC-07, UC-15
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-SAF-01 | The host shall compute a Sun keep-out zone (default: optical axis at least 15° from the Sun, configurable) and refuse or re-plan any motion entering it. | M | T | P060 |
| FR-SAF-02 | The host shall send the keep-out zone to the controller in mount coordinates; the controller shall enforce the last zone received and, if it has not been refreshed within 120 s, allow motion only to the park position. | M | T | P060 |
| FR-SAF-03 | The controller shall enforce soft limits independently of the host. | M | T | P051, P060 |
| FR-SAF-04 | If the controller receives no heartbeat for 1.5 s it shall stop motion and, after a configurable delay, park. | M | T | P050, P060 |
| FR-SAF-05 | An emergency stop (UI button, keyboard shortcut, optional hardware input) shall halt all motion and block motion commands until explicitly cleared. | M | T | P060 |
| FR-SAF-06 | The system shall detect stalled or non-responding axes when the hardware allows it (IMU/encoder disagreement) and stop the axis. | S | T | P057, P060 |
| FR-SAF-07 | The system shall stop missions and park when enclosure temperature exceeds a configurable limit or rain is detected (if sensors are present). | S | T | P060, P081 |
| FR-SAF-08 | Every safety intervention shall be logged with its cause and shown to the user. | M | T | P060 |

### 2.9 Operating modes (FR-MOD) — UC-09
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-MOD-01 | The system shall have Manual, Assisted and Autonomous modes with defined, logged transitions. | M | T | P073 |
| FR-MOD-02 | An operator command shall always pre-empt autonomous behaviour (operator override). | M | T | P073 |
| FR-MOD-03 | Illegal mode transitions (e.g. Autonomous while e-stop is active) shall be rejected with a reason. | M | T | P073 |
| FR-MOD-04 | In Assisted mode the system shall suggest actions (e.g. exposure, re-pointing) that the operator confirms. | C | D | P073 |

### 2.10 Missions (FR-MIS) — UC-09
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-MIS-01 | Missions shall be declarative files (steps, conditions, schedules) validated before they run. | M | T | P075 |
| FR-MIS-02 | Any mission shall be executable as a dry run on the simulator, producing a timeline. | M | T | P075 |
| FR-MIS-03 | The system shall provide a zenith cloud-base-height sampling mission. | M | D | P076 |
| FR-MIS-04 | The system shall provide an all-sky survey mission that stitches pointings into a sky map. | S | A | P077 |
| FR-MIS-05 | The system shall provide an adaptive time-lapse mission scheduled from computed sunrise/sunset. | M | D | P078 |
| FR-MIS-06 | The system shall provide a cloud-tracking mission that keeps a selected cloud in view while respecting limits and keep-out. | S | A | P079 |
| FR-MIS-07 | The system shall support event triggers (e.g. a cloud class detected, rapid change in cloud cover) that start captures and notifications. | S | T | P080 |
| FR-MIS-08 | After a fault or power loss, missions shall resume where safe or stop with a recorded reason. | M | T | P081 |

### 2.11 STRATIA inference (FR-AI) — UC-10, UC-16
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-AI-01 | The system shall load STRATIA ONNX models, validate `model_card.json` against the supported contract version, and refuse incompatible models. | M | T | P065 |
| FR-AI-02 | The system shall select an execution provider automatically (CUDA → DirectML → CPU on Windows; CPU on Raspberry Pi; optional accelerator) and report which one is used. | M | T | P066 |
| FR-AI-03 | Inference shall run asynchronously at a configurable rate without blocking capture or the UI, dropping frames rather than queuing them. | M | T | P066 |
| FR-AI-04 | The system shall build the model's geometry and metadata inputs (ray map, Sun position, solar time) from the active calibration, pose, time and site, and mark them unknown when unavailable. | M | T | P067 |
| FR-AI-05 | The system shall display sky/cloud/glare masks, cloud layers, cloud genus with confidence, cloud cover and cloud-base height with its uncertainty interval. | M | D | P068 |
| FR-AI-06 | When the model's reliability score or its inputs indicate an out-of-domain frame, results shall be marked "uncertain" and overlays drawn only from valid outputs. | M | T | P068 |
| FR-AI-07 | Results shall be logged per frame and plotted as timelines. | M | T | P069 |
| FR-AI-08 | The system shall install, update and roll back models with checksum verification. | S | T | P071 |
| FR-AI-09 | The system shall provide an inference benchmark per device and execution provider. | S | A | P070 |

### 2.12 Remote access (FR-REM) — UC-11, UC-12, UC-17
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-REM-01 | The daemon shall own all devices and run without a display, starting at boot as a service. | M | D | P083 |
| FR-REM-02 | The daemon shall expose a REST API described by an OpenAPI 3 specification. | M | T | P084 |
| FR-REM-03 | The daemon shall stream telemetry, events, logs and AI results over WebSocket. | M | T | P085 |
| FR-REM-04 | The daemon shall stream live video as MJPEG on the local network. | M | A | P086 |
| FR-REM-05 | The daemon shall stream live video over WebRTC with bandwidth adaptation. | S | A | P086 |
| FR-REM-06 | A web dashboard served by the daemon shall provide live view, controls, missions and the gallery, usable on a phone. | M | D | P088 |
| FR-REM-07 | The desktop application shall work as a client of a remote daemon with the same features as locally. | M | D | P089 |
| FR-REM-08 | Only one client at a time shall hold the control lock; others are read-only. The lock shall time out when its holder disconnects. | M | T | P087 |
| FR-REM-09 | The daemon shall publish telemetry and AI results over MQTT with Home Assistant discovery. | S | T | P085 |
| FR-REM-10 | The daemon shall expose CloudScope as ASCOM Alpaca SafetyMonitor and ObservingConditions devices so other astronomy software can use its sky assessment; disabled by default and limited to the local network. | S | T | P084 |

### 2.13 Security (FR-SEC) — UC-11, UC-12
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-SEC-01 | Remote access shall be disabled by default (daemon bound to localhost) until the owner enables it. | M | T | P087 |
| FR-SEC-02 | Every remote request shall be authenticated; there shall be no default or hard-coded credentials. | M | T | P087 |
| FR-SEC-03 | The system shall support viewer, operator and admin roles; only operators and admins can move hardware or change settings. | M | T | P087 |
| FR-SEC-04 | Non-localhost connections shall use TLS. | M | T | P087 |
| FR-SEC-05 | Security-relevant actions (sign-in, control-lock changes, settings changes) shall be written to an audit log. | S | T | P087 |
| FR-SEC-06 | Secrets (tokens, keys) shall never be written to logs, sidecars or the repository. | M | I | P087 |

### 2.14 Data management (FR-DAT) — UC-13, UC-14
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-DAT-01 | The system shall catalogue every captured frame and its metadata in a local database, queryable by time and metadata. | M | T | P030 |
| FR-DAT-02 | The system shall browse sessions with thumbnails, filters and metadata. | M | D | P040 |
| FR-DAT-03 | The system shall export selections in STRATIA's ingest format with checksums. | M | T | P091 |
| FR-DAT-04 | The export shall optionally blur people and number plates when ground is visible. | S | A | P091 |
| FR-DAT-05 | The system shall export to and import labels from Label Studio and CVAT. | S | T | P092 |
| FR-DAT-06 | The system shall synchronise captures to remote storage (rsync or S3-compatible) with integrity checks. | C | T | P093 |
| FR-DAT-07 | The system shall apply a configurable retention policy without deleting data that is not yet synchronised or exported. | S | T | P030 |

### 2.15 Platform and packaging (FR-PLT) — UC-17
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| FR-PLT-01 | The system shall run on Windows 10/11 x64, Ubuntu 24.04 x64 and Raspberry Pi OS based on Debian 13 "Trixie" (64-bit) on a Raspberry Pi 5, with Qt 6.8 or newer. | M | T | P012, P098 |
| FR-PLT-02 | The system shall provide a Windows installer, Debian packages (amd64, arm64) with a systemd unit, and released firmware binaries. | M | D | P098 |
| FR-PLT-03 | Configuration shall be stored in human-readable files, validated against a schema, and migrated automatically between versions. | M | T | P013 |

---

## 3. Non-functional requirements

### 3.1 Performance (NFR-PERF)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-PERF-01 | Live preview at a camera mode of at most 1920×1080 shall reach at least 15 fps on the reference laptop and 10 fps on a Raspberry Pi 5, when the camera supplies that rate. | M | A | P032, P096 |
| NFR-PERF-02 | Saving a full-resolution (16 MP) JPEG with its sidecar shall take at most 1 s on the reference laptop and 3 s on a Raspberry Pi 5. | M | A | P027, P096 |
| NFR-PERF-03 | User input shall produce visible feedback within 100 ms. | M | A | P044 |
| NFR-PERF-04 | During a 30 s interval sequence with AI enabled, average CPU use on a Raspberry Pi 5 shall stay at or below 70% without thermal throttling (active cooler fitted). | S | A | P096 |
| NFR-PERF-05 | STRATIA inference latency targets are set from STRATIA P094/P096 measurements and recorded in the model card; the UI shall never block on inference. | M | A | P066 |
| NFR-PERF-06 | Live video latency on the local network shall be at most 300 ms (MJPEG). | S | A | P086 |

### 3.2 Reliability (NFR-REL)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-REL-01 | The system shall run 72 hours of autonomous operation without an unrecovered fault or data loss. | M | D | P082 |
| NFR-REL-02 | After a camera is unplugged and replugged, capture shall resume within 60 s. | M | T | P029, P081 |
| NFR-REL-03 | A power loss shall lose at most the frame being written; no corrupted file shall exist under a final name. | M | T | P027, P081 |
| NFR-REL-04 | The daemon shall restart automatically after a crash, and missions shall resume per FR-MIS-08. | M | T | P083 |
| NFR-REL-05 | Memory use shall not grow during a 24-hour capture run (no leaks above 5% growth after warm-up). | M | A | P032 |

### 3.3 Safety (NFR-SAFE)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-SAFE-01 | An emergency stop shall halt motion within 100 ms of the command reaching the controller. | M | A | P060 |
| NFR-SAFE-02 | Safety functions (keep-out, soft limits, heartbeat loss) shall be enforced in both host and firmware, and each shall have a fault-injection test on the simulator and on hardware. | M | T | P060, P063 |
| NFR-SAFE-03 | Servo power shall be supplied separately from logic power, as specified in the hardware reference design. | M | I | P009, P095 |

### 3.4 Security (NFR-SEC)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-SEC-01 | A threat model shall be documented and reviewed before remote access ships. | M | I | P087 |
| NFR-SEC-02 | Dependencies shall be scanned for known vulnerabilities in CI, and secrets scanning shall run on every push. | S | T | P012, P015 |

### 3.5 Usability (NFR-USE)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-USE-01 | Every visible control shall perform a real function; unavailable features shall be hidden or visibly disabled with the reason. | M | I | P046, P097 |
| NFR-USE-02 | Every value shall show its unit; times shall show UTC and local time. | M | I | P044 |
| NFR-USE-03 | The UI shall be readable on high-DPI displays and scale correctly. | M | D | P044 |
| NFR-USE-04 | Error messages shall state what happened and what the user can do. | M | I | P044 |

### 3.6 Portability and maintainability (NFR-MNT)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-MNT-01 | The code shall build from a clean checkout with one documented command per platform. | M | T | P011 |
| NFR-MNT-02 | CI shall build and test Windows x64, Linux x64 and Linux arm64 on every push; pushes to `master` require green CI from P012 on. | M | T | P012 |
| NFR-MNT-03 | Core library line coverage shall be at least 70%. | S | A | P014 |
| NFR-MNT-04 | New code shall pass formatting and static analysis with zero warnings. | M | T | P015 |
| NFR-MNT-05 | Every phase shall be documented per `CONTRIBUTING.md`; every architecture decision shall have an ADR. | M | I | all |

### 3.7 Data integrity (NFR-DATA)
| ID | Requirement | Pri | Ver | Phase |
|---|---|---|---|---|
| NFR-DATA-01 | All stored timestamps shall be UTC with explicit offset; the time source (host clock, NTP, GPS) shall be recorded. | M | T | P022, P053 |
| NFR-DATA-02 | Metadata shall distinguish measured values from declared or defaulted ones. | M | I | P027 |
| NFR-DATA-03 | Simulated data shall always be labelled as simulated in files, the UI and the API. | M | T | P018 |

---

## 4. Constraints and assumptions

| # | Constraint / assumption | Consequence |
|---|---|---|
| C1 | Development hardware: one Windows laptop (RTX 4050), Arducam B0268 (USB), no Raspberry Pi or controller yet | Simulator-first development (P018); hardware phases verified when parts arrive |
| C2 | Qt is used under the LGPL with dynamic linking; GPL-only Qt modules (e.g. Qt HTTP Server, Qt Graphs) are excluded (ADR-010) | Packaging must ship Qt as shared libraries with licence notices; HTTP server is Drogon (ADR-006) |
| C3 | The Arduino Uno has 2 KB of RAM | Uno is limited to CSDP-Lite (FR-FW-02) |
| C4 | Location: Bengaluru, India (hot season, monsoon) | Enclosure and thermal requirements in P094/P095 |
| C5 | STRATIA model availability depends on the STRATIA repository | Stage F starts after STRATIA exports a model; a contract-conformant dummy model is used for testing earlier |
| C6 | B0268 capabilities (modes, controls) are not yet measured | Confirmed in P009 research and P020/P021 measurements |

## 5. Open questions (owner and phase)

| # | Question | Resolved in |
|---|---|---|
| Q1 | Default keep-out half-angle for a ~105° lens that always sees much of the sky | P060 (with measurements of glare and sensor behaviour) |
| Q2 | Servo vs stepper pan-tilt for the target pointing accuracy | P009, P057 |
| Q3 | INDI or ASCOM Alpaca compatibility | Resolved in P006 / ADR-011: Alpaca client and server (should), INDI client on Linux (could) |
| Q4 | Which accelerator (if any) for Raspberry Pi inference | P066, after STRATIA P095 |

## 6. Summary and traceability

| Group | M | S | C | Total |
|---|---|---|---|---|
| FR-CAM | 7 | 3 | 2 | 12 |
| FR-DSP | 7 | 4 | 0 | 11 |
| FR-REC | 7 | 3 | 2 | 12 |
| FR-SEQ | 6 | 0 | 0 | 6 |
| FR-CAL | 4 | 2 | 0 | 6 |
| FR-CTL | 9 | 3 | 1 | 13 |
| FR-FW | 3 | 1 | 1 | 5 |
| FR-SAF | 6 | 2 | 0 | 8 |
| FR-MOD | 3 | 0 | 1 | 4 |
| FR-MIS | 5 | 3 | 0 | 8 |
| FR-AI | 7 | 2 | 0 | 9 |
| FR-REM | 7 | 3 | 0 | 10 |
| FR-SEC | 5 | 1 | 0 | 6 |
| FR-DAT | 3 | 3 | 1 | 7 |
| FR-PLT | 3 | 0 | 0 | 3 |
| NFR (all) | 24 | 4 | 0 | 28 |
| **Total** | **106** | **34** | **8** | **148** |

Counts generated by parsing this file (148 requirements, no duplicate IDs).

Traceability rule: each requirement's acceptance test is `AT-<ID>` (P097); each phase document lists the requirement IDs it covers; `docs/requirements/traceability.csv` (generated from this file in P012 by a CI script) maps requirement → phase → test → status.
