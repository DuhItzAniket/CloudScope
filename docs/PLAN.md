# CloudScope — System Implementation Plan (100 phases)

Repo: https://github.com/DuhItzAniket/CloudScope · Plan date: 2026-10-04

---

## 0. What CloudScope is (scope lock)

CloudScope is the **complete sky-observation system**. STRATIA is only the brain it loads; everything else lives here:

| Subsystem | Responsibility |
|---|---|
| **Camera application** | SharpCap/FireCapture-class capture software: device control, live view, histogram/scopes, focus and exposure tools, sequencing, recording (PNG/TIFF/FITS/SER/MP4), sessions |
| **Control system** | Hardware abstraction for pan-tilt servos/steppers, IMU, GPS/RTC, environment sensors; firmware for ESP32 and Arduino Uno; native Raspberry Pi 5 GPIO/I2C; calibration; safety (sun keep-out, watchdog, e-stop) |
| **Operating modes** | Manual (operator drives everything), Assisted, Autonomous (missions: zenith CBH sampling, sky survey, time-lapse, cloud tracking, event triggers) |
| **Remote access** | Headless daemon, REST + WebSocket API, video streaming, web dashboard, desktop app as remote client, authentication/TLS |
| **STRATIA integration** | ONNX inference with calibration-aware metadata, overlays, logging, model management |
| **Data pipeline** | Session catalog and export of captured data back to STRATIA for training/labeling |

### Platforms
| Tier | Hardware | Runs |
|---|---|---|
| Host — full | Windows/Linux laptop (your Legion 5i) | Desktop app + daemon + GPU inference (CUDA/DirectML) |
| Host — embedded | Raspberry Pi 5 (8 GB recommended) | Daemon + web UI (+ optional desktop app on HDMI); CPU/INT8 inference; optional Raspberry Pi AI HAT+ (box-mounted; the AI Kit is discontinued) |
| Controller — full | ESP32 (USB serial or Wi-Fi) | Servo control (PCA9685 or direct PWM), IMU fusion, GPS/RTC/env sensors, watchdog |
| Controller — lite | Arduino Uno R4 Minima (reference) or Uno R3 (USB serial) | Servos + heartbeat + e-stop; IMU via BNO085 UART-RVC on R4, legacy MPU-6050 on R3 |
| Controller — native | Raspberry Pi 5 GPIO/I2C | PCA9685 + IMU directly, no microcontroller |
| Controller — none | Fixed camera | Everything except motion |

> An **Arduino Uno cannot host CloudScope** — it has no OS and 2 KB RAM. It is supported as a *peripheral controller* attached to a laptop or Pi.

### Quality bar ("not AI slop")
- Every visible control does something real and has a test; no placeholder buttons, no fake data, no simulated values presented as real.
- Every feature is traceable to a requirement (SRS, P005) and verified in an acceptance test (P097).
- Safety logic is enforced in **two places**: host planner and microcontroller firmware.
- Reference products studied in P006 (SharpCap, AMCap, FireCapture, NINA, KStars/Ekos/INDI, indi-allsky, Thomas Jacquin's allsky) — we match their fundamentals before adding AI.

---

## 1. Architecture decisions (finalized as ADRs in P008)

| ADR | Decision | Reason |
|---|---|---|
| 001 | **C++20 + Qt ≥ 6.8**, CMake | Keeps the existing toolchain (Qt 6.9 / MSVC already installed); Raspberry Pi OS (Debian 13 "Trixie") ships Qt 6.8.2; native performance on Pi 5; one codebase for Windows + Linux aarch64 |
| 002 | **Desktop UI in Qt Widgets + Qt Advanced Docking System** (replaces legacy QML) | Dockable, dense "workbench" UI like SharpCap; QML kept only if a touch UI is needed later |
| 003 | **Client/daemon split**: `cloudscope-core` (library) → `cloudscoped` (headless daemon owning devices) → clients (desktop app, web UI). Desktop app can also embed the core in-process for zero-latency local use | Same code path for local and remote; Pi runs headless |
| 004 | **ONNX Runtime** for STRATIA (CUDA → DirectML → CPU on Windows; CPU/XNNPACK on Pi; optional Raspberry Pi AI HAT+ via a separately compiled HEF model) | Matches STRATIA export; legacy ORT GPU path already proven (21 fps) |
| 005 | **CSDP — CloudScope Device Protocol**: COBS framing + CRC-16/CCITT + versioned fixed-layout messages, generated from one YAML schema into C (firmware) and C++ (host) | Small enough for an Uno, robust over serial and Wi-Fi |
| 006 | **Remote API**: REST (OpenAPI 3) + WebSocket via **Drogon** (MIT) — Qt HTTP Server is GPL-only (see ADR-006); video via MJPEG first, WebRTC (libdatachannel) later | Permissive licence; works through browsers |
| 007 | Config in TOML (toml++) with JSON-schema validation and migrations; logs via spdlog | — |
| 008 | Image formats: PNG/TIFF-16, JPEG, **FITS** (cfitsio, full headers), **SER** video, MP4 via FFmpeg (optional) | Astronomy/meteorology interoperability |
| 009 | Firmware: PlatformIO; ESP32 on Arduino-ESP32 core + FreeRTOS tasks; Uno on Arduino core | Fast to build, easy for you to flash |
| 010 | Licence (your choice in P001; default Apache-2.0); Qt linked dynamically (LGPL compliance) | — |

```
CloudScope/
├── legacy/                 # AI-Day v0.1 code (frozen, tagged v0.1-aiday)
├── core/                   # libcloudscope-core: camera, capture, control, autonomy, inference, storage
├── daemon/                 # cloudscoped: headless service, REST/WS, streaming
├── apps/desktop/           # Qt Widgets workbench
├── web/                    # web dashboard (static, served by daemon)
├── firmware/{esp32,uno,common}/   # CSDP firmware
├── protocol/               # CSDP YAML schema + generators
├── hardware/               # CAD (enclosure), wiring diagrams, BOM
├── tools/                  # calibration tools, sky logger, build scripts
├── tests/                  # unit, integration, HIL, UI
├── packaging/              # Windows installer, .deb, systemd
└── docs/{phases,adr,arch,manual,api,hardware}
```

---

## 2. Phase protocol (same as STRATIA)

Each phase: create `docs/phases/P###-<slug>.md` (template below) → implement → verify exit criteria with evidence → update `PROJECT_STATE.md` + `CHANGELOG.md` → `git commit -m "P###: <title>"` (trailer `Phase-Status: DONE|PARTIAL|BLOCKED`) → `git push origin master`. Stage end: `git tag stage-<X>-complete`. CI must be green before push from P012 onward. Hardware phases attach photos/oscilloscope or log captures as evidence.

```markdown
# P### — <title>
Status: DONE | PARTIAL | BLOCKED     Date:     Commit:
## Objective
## Requirements covered (FR/NFR IDs)
## Design notes / ADR links
## Work log
## Verification (tests, measurements, screenshots, logs)
## Exit criteria — checklist with evidence
## Safety & failure-mode notes
## Deviations & next phase
```

---

## 3. The 100 phases

Legend — **NOW** = needed immediately to feed STRATIA with data; **HW** = needs physical hardware; otherwise runs on the simulator.

### Stage A — Re-foundation & requirements (P001–P010)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P001 | Legacy freeze | Tag current `main` as `v0.1-aiday`; move scripts/, segtools/, desktop/, models/, results/ into `legacy/` (history preserved via `git mv`); README explains the reboot | Tag pushed; legacy builds docs still readable | NOW |
| P002 | Docs & phase protocol | `docs/{phases,adr,arch}`, template, `PROJECT_STATE.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, commit convention; port `.github/history` index into `legacy/docs` | P001–P002 docs follow template | NOW |
| P003 | **Interim Sky Logger** | Small Python/OpenCV CLI (`tools/sky_logger/`): fixed interval capture from B0268, **manual exposure lock**, UTC timestamps, JSON sidecar (exposure, gain, WB, pointing note), disk guard; runs on laptop and Pi 5 | 24-hour unattended run; sidecars valid; STRATIA P019 ingests output | NOW |
| P004 | Use cases & personas | Researcher, field operator, remote viewer; use cases (manual imaging, autonomous survey, CBH sampling, remote monitoring, dataset collection) | Reviewed by you | |
| P005 | SRS | Functional (FR) + non-functional (NFR: fps, latency, Pi CPU budget, 72 h reliability, safety, security) requirements; traceability matrix skeleton | Every FR has an ID and an acceptance test idea | |
| P006 | Competitive analysis | Feature matrix vs SharpCap, AMCap, FireCapture, NINA, KStars/Ekos/INDI, indi-allsky, allsky; adopt/skip decisions (incl. INDI/ASCOM compatibility ADR) | Matrix committed | |
| P007 | System architecture | C4 diagrams (context, container, component), threading model, data flow, failure domains | Reviewed | |
| P008 | ADRs 001–010 | Write decisions in Section 1 as ADRs | All accepted | |
| P009 | Hardware reference designs | BOM per tier, wiring diagrams (ESP32-S3+PCA9685+BNO085, steppers for precision, Pi 5 direct GPIO, Uno R4/R3 lite), power budget (separate servo rail), thermal split (sky head vs electronics box) | Designs in `docs/hardware/`, facts in `docs/research/` | |
| P010 | **Gate R — Requirements & architecture review** | Walkthrough of P004–P009 | Signed | |

### Stage B — Engineering foundation (P011–P018)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P011 | Build system | CMake presets (windows-msvc, linux-x64, linux-aarch64), vcpkg manifest (OpenCV, spdlog, fmt, toml++, nlohmann-json, cfitsio, libjpeg-turbo, Catch2), Qt via aqtinstall (Windows) / system or built Qt (Pi) | Hello-core builds on Windows + Linux | |
| P012 | CI | GitHub Actions: Windows, Ubuntu x64, ubuntu-24.04-arm (Pi-like aarch64); build + tests + artifacts | Green on all three | |
| P013 | Core utilities | Logging, `expected<T,E>` errors, config + schema + migration, strong unit types (deg/rad, ms), UTC/monotonic clocks | Unit tests | |
| P014 | Test infrastructure | Catch2, Qt Test, recorded-frame fixtures, coverage report | Coverage in CI | |
| P015 | Static analysis | clang-format, clang-tidy, ASan/UBSan on Linux CI, gitleaks | Zero warnings policy on new code | |
| P016 | Threading & event bus | Typed signals, worker pools, lock-free SPSC frame ring buffer; benchmark | ≥ 4K frames copied at > 60 fps without drops (bench) | |
| P017 | HAL interfaces | `ICamera`, `IMount`, `IImu`, `ISensor`, `ITransport`, `IInference`; registry/factory; capability model | Interfaces documented; mock implementations | |
| P018 | Simulators | Virtual camera (replays sequences/Eye2Sky/B0268 sessions), virtual mount (kinematics, latency, noise, limits), virtual IMU/GPS | All later phases testable without hardware | |

### Stage C — Camera subsystem (P019–P032)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P019 | Device enumeration | Media Foundation (Windows), V4L2 (Linux), optional libcamera (Pi CSI); stable device IDs; hot-plug | B0268 + laptop webcam detected on both OSes | HW |
| P020 | Modes | Resolution/fps/pixel format negotiation (MJPEG/YUYV; RAW8/RAW16/MONO16, ROI and binning where the camera supports them); measured B0268 mode table | Table in docs (actual fps per mode) | HW |
| P021 | Camera controls | Absolute exposure, gain, WB, brightness/contrast/saturation, auto on/off; capability discovery; profiles | Each control verified by measuring frame brightness response | HW |
| P022 | Acquisition pipeline | Capture thread, host monotonic + UTC timestamps, drop detection, frame metadata | Drop counter accurate under stress | HW |
| P023 | Decode & colour | libjpeg-turbo MJPEG, YUYV→RGB, 8/16-bit paths | Decode latency table | |
| P024 | Frame statistics | Histogram, clipping %, mean/median, noise estimate, saturation map, sun-blob detection | Unit tests on synthetic frames | |
| P025 | Sky auto-exposure + HDR | Sun-aware percentile AE that protects cloud highlights; bracketing; Mertens/Debevec merge | Fewer clipped cloud pixels than camera auto-exposure on test scenes | HW |
| P026 | Calibration frames | Dark frames, flat field, vignetting correction | Flat-corrected frame uniformity improved (measured) | HW |
| P027 | Recording I | PNG/TIFF-16 with AstroTIFF header, JPEG, FITS with the keyword set in `docs/research/P006_competitive_analysis.md` (DATE-OBS UTC, TIMESYS, MJD-OBS, EXPTIME, GAIN, OBSGEO-B/L/H, SITELAT/SITELONG, CENTALT/CENTAZ, ROWORDER, calibration ID; WCS when calibrated), JSON sidecar | Files open in Siril/DS9 with correct orientation and keywords; sidecar schema valid | |
| P028 | Recording II | SER v3 video with UTC trailer (endianness flag set by round-trip test), MP4/H.264 (FFmpeg, optional), time-lapse assembly, keograms and star trails | SER opens in SER Player and Siril; keogram matches sequence | |
| P029 | Capture sequencer | Single, burst, interval, bracket, scheduled; day/night capture profiles switched at a configurable Sun elevation; filename templates; disk guard | 1,000-frame interval run without error; profile switch at the configured elevation | |
| P030 | Sessions & catalogue | Session folder layout; SQLite catalogue of frames + metadata; retention policy | Query 100k frames < 100 ms | |
| P031 | Intrinsic calibration tool | Checkerboard capture assistant; OpenCV fisheye/omnidir fit; export in STRATIA camera-model format | Reprojection error < 0.5 px; file loads in STRATIA P043 | HW |
| P032 | Camera soak test | 24 h capture on laptop and Pi 5; fps, latency, memory, temperature | No leaks/crashes; report | HW |

### Stage D — Desktop application (P033–P046)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P033 | App shell | Main window, docking, layout save/restore, dark theme + red night-vision theme | Layouts persist across restarts | |
| P034 | Live view | QRhi/OpenGL renderer, zoom/pan, 1:1, fit, FPS & drop indicators | 4656×3496 MJPEG preview smooth on laptop | |
| P035 | Camera control panel | All controls with real ranges, auto toggles, profiles | Every control round-trips to device | |
| P036 | Histogram, scopes & stretch | Live histogram (lin/log), clipping indicators, RGB parade; display stretch (manual/auto) applied to preview only; over-exposure highlight | Matches offline computation; recorded files unaffected by stretch | |
| P037 | Overlays | Reticle, grid, altitude circles & compass (from calibration + pose), sun marker & keep-out zone | Overlay accuracy checked against sun position | |
| P038 | Image tools | Focus aid (Laplacian variance), loupe, pixel inspector, ROI stats | Unit tests on known images | |
| P039 | Capture panel | Formats, sequencer UI, progress, countdown | Drives P029 end-to-end | |
| P040 | Session browser | Thumbnails, metadata, filters, compare, export | Browses 100k-frame catalogue smoothly | |
| P041 | Preferences | Devices, paths, units, hotkeys; validated config | Invalid config rejected with clear message | |
| P042 | Console & status | Log console, notifications, status bar (camera, mount, sensors, AI, remote clients, disk) | All states visible and accurate | |
| P043 | Input devices | Keyboard shortcuts; gamepad via SDL2 GameController | Gamepad drives simulated mount | |
| P044 | UX polish | High-DPI, scaling, tooltips, i18n-ready strings, accessibility names | UX checklist | |
| P045 | UI test automation | Qt Test interactions + screenshot regression | Runs in CI (offscreen) | |
| P046 | **Gate D — Workbench review** | Compare against SharpCap feature matrix (P006) | Must-have features = 100% | |

### Stage E — Hardware control system (P047–P064)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P047 | CSDP spec v1 | Framing, message catalogue (HELLO/capabilities, heartbeat, servo/motion, IMU stream, sensor read, config, fault, e-stop), versioning; YAML schema + codegen | Spec + generated code compile for ESP32, Uno, host | |
| P048 | Host transports | Serial (QSerialPort), TCP/UDP (ESP32 Wi-Fi), WebSocket; reconnect; latency measurement | Round-trip latency table | HW |
| P049 | Discovery & capabilities | Port scan, HELLO handshake, capability bitmap → UI/planner adapt automatically | Swapping ESP32 ↔ Uno changes available features without config edits | HW |
| P050 | ESP32 firmware base | PlatformIO, FreeRTOS tasks, CSDP stack, hardware watchdog, NVS config, OTA update | 24 h link soak without desync | HW |
| P051 | ESP32 actuation | PCA9685 (I2C) and direct LEDC PWM; per-channel pulse↔angle calibration; slew-rate limits | Commanded vs measured angle within servo spec | HW |
| P052 | ESP32 IMU | BNO085 (reference; Game Rotation Vector near motors), BMI270/LSM6DSOX 6-axis fallback with Madgwick fusion, legacy MPU-6050; magnetometer calibration only where no motor magnets are near | Static attitude error < 1–2° (documented method) | HW |
| P053 | ESP32 aux sensors | GPS (NEO-M8N: time + location), DS3231 RTC, BME280, rain sensor, light sensor, limit switches | Values streamed + logged | HW |
| P054 | Arduino Uno CSDP-Lite | Uno R4 Minima (reference) and R3: servos + heartbeat + e-stop; IMU via BNO085 UART-RVC on R4 Serial1, legacy MPU-6050 on R3; RAM/flash budget report | Works with host; < 75% RAM used on each board | HW |
| P055 | Native & standard HALs | Raspberry Pi 5 libgpiod / i2c-dev (PCA9685 + IMU direct, hardware PWM); ASCOM Alpaca client (discovery, Telescope alt-az, Focuser, Switch, ObservingConditions); optional INDI client on Linux | Same tests as ESP32 tier pass; Alpaca simulator passes ConformU-style checks | HW |
| P056 | Mount kinematics | Pan/tilt ↔ az/el, mechanical offsets, backlash model | Unit tests | |
| P057 | Motion control | Trapezoidal/S-curve trajectories, soft limits, homing, park, IMU closed-loop correction | Overshoot/settling measured | HW |
| P058 | IMU–camera extrinsics | Hand-eye calibration (checkerboard or sky features) | Residual reported | HW |
| P059 | Pointing model | Fit using sun positions (short exposures, keep-out respected), landmarks; az/el accuracy | Pointing RMS reported | HW |
| P060 | Safety system | **Sun keep-out cone** in planner *and* firmware; heartbeat loss → safe park; e-stop; stall/overcurrent (if sensed); thermal limits | Fault-injection tests pass on sim and hardware | HW |
| P061 | Mount control UI | Virtual joystick, D-pad, goto az/el, sky-dome widget (FOV footprint, sun, keep-out) | Drives sim + hardware | |
| P062 | Telemetry | Attitude display, sensor dashboard, telemetry logging to catalogue | Plots replay correctly | |
| P063 | HIL rig & sim parity | Same test suite runs against simulator and real hardware | Parity report | HW |
| P064 | **Gate E — Control acceptance** | Pointing accuracy, latency, safety tests | Signed | HW |

### Stage F — STRATIA integration (P065–P072)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P065 | Contract implementation | Load `model_card.json` (stratia-contract v1), validate version/IO, reject incompatible models | Contract test with STRATIA's exported dummy model | |
| P066 | Inference engine | ORT session per EP (CUDA→DirectML→CPU; CPU/XNNPACK on Pi; optional AI HAT+ with a HEF model compiled on x86-64 Linux by the Hailo DFC); async scheduler with frame skipping; preprocessing | Latency table per device/EP; outputs equal Python reference ≤1e-4 | |
| P067 | Metadata provider | UTC + lat/lon (GPS/config) + intrinsics (P031) + pose (IMU/mount) → `ray_map` + `meta` tensors | Ray map matches STRATIA's generator on same inputs (cross-repo test) | |
| P068 | AI overlays | Sky-parse & layer masks, genus + confidence/top-k, oktas, CBH with interval, OOD warning badge | Overlays only drawn when valid; "uncertain" state shown honestly | |
| P069 | AI logging & charts | Per-frame results to catalogue; timelines (cover, CBH, genus) | Charts match logged data | |
| P070 | Inference benchmark tool | CLI + UI benchmark per EP/device | Report committed | |
| P071 | Model manager | Install/update/rollback models with checksums | Rollback tested | |
| P072 | **Gate F — AI pipeline** | End-to-end on recorded sequences and live camera | Signed | |

### Stage G — Operating modes & autonomy (P073–P082)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P073 | Mode framework | Manual / Assisted / Autonomous state machine; operator override always wins; mode audit log | State-machine tests incl. illegal transitions | |
| P074 | Manual mode complete | Full manual control, presets, goto, capture | Manual acceptance checklist | |
| P075 | Mission engine | Declarative missions (YAML): steps, conditions, schedules; validator; dry-run on simulator | Invalid missions rejected; dry-run timeline | |
| P076 | Mission: zenith CBH sampling | Periodically point to zenith → capture (AE locked) → STRATIA CBH → log | Runs 24 h on sim, then hardware | |
| P077 | Mission: sky survey | Grid of pointings → stitched sky map using calibration + pose | Mosaic geometric error reported | |
| P078 | Mission: adaptive time-lapse | Day-long exposure adaptation; sunrise/sunset schedule computed from location | 1 full day captured without clipping failures | |
| P079 | Mission: cloud tracking | Use STRATIA segmentation + motion to keep a selected cloud in view, respecting keep-out and limits | Tracking error metric on recorded/sim sequences | |
| P080 | Mission: event triggers | e.g. Cb detected / rapid cover change → burst capture + notification | Trigger precision on recorded data | |
| P081 | Fault handling | Camera unplug, MCU reset, disk full, power loss → resume; health monitor | Fault-injection suite passes | |
| P082 | **Gate G — 72 h autonomy run** | Simulator first, then real hardware outdoors | No unrecovered faults; data complete | HW |

### Stage H — Remote access (P083–P090)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P083 | `cloudscoped` daemon | Headless service owning devices; systemd unit (Pi), Windows service option | Survives reboot; desktop app attaches | |
| P084 | REST API & Alpaca server | OpenAPI 3 spec; devices, camera, capture, missions, sessions, models; ASCOM Alpaca SafetyMonitor + ObservingConditions server (off by default, LAN only) | Contract tests generated from spec; an Alpaca client reads CloudScope's safety state | |
| P085 | Event channels | WebSocket (telemetry, events, logs, AI results) and MQTT with Home Assistant discovery | Load test: 5 clients; entities appear in Home Assistant | |
| P086 | Video streaming | MJPEG preview (LAN), then WebRTC (libdatachannel) with bandwidth adaptation | Latency < 300 ms LAN (measured) | |
| P087 | Security | Token auth, roles (viewer/operator/admin), TLS, single-controller lock, audit log, threat model | Security test checklist; no default passwords | |
| P088 | Web dashboard | Static SPA served by daemon: live view, controls, missions, gallery; mobile layout | Works on phone browser | |
| P089 | Desktop remote client | Desktop app connects to remote daemon with same UI | Identical behaviour local vs remote | |
| P090 | **Gate H — Remote ops** | Guide for Tailscale/WireGuard (no port-forwarding needed); end-to-end remote test | Signed | |

### Stage I — Data pipeline for STRATIA (P091–P093)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P091 | Dataset export | Sessions → STRATIA ingest format (images + sidecars: UTC, exposure, calib, pose, site); privacy filter (blur people/plates if ground visible) | STRATIA P019 ingests without manual fixes | |
| P092 | Labeling bridge | Export to Label Studio/CVAT; import labels back into catalogue | Round-trip test | |
| P093 | Sync | rsync / S3-compatible upload with checksums | Corruption test passes | |

### Stage J — Enclosure & field hardware (P094–P095)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P094 | Enclosure v2 | 3D-printed (PETG/ASA UV-resistant) housing, window/dome, sun shade, dew heater/ventilation, cable glands, pan-tilt mechanics; CAD + print settings in `hardware/` | Outdoor 72 h test: no water ingress, temperature logged | HW |
| P095 | Electrical | Harness, 5 V 5 A Pi supply + separate servo rail, fuses/TVS, assembly guide with photos | Power-budget measurements | HW |

### Stage K — QA & release (P096–P100)

| ID | Phase | Work | Exit criteria | |
|---|---|---|---|---|
| P096 | Performance | Profile laptop & Pi 5 (CPU %, thermal throttling, memory, fps with AI on) | NFR targets met or documented | HW |
| P097 | System acceptance | Run full SRS traceability matrix | All must-have FRs pass | HW |
| P098 | Packaging | Windows installer (Qt IFW/WiX), `.deb` (amd64/arm64) + systemd, Pi setup script, firmware binaries | Clean-machine install works on both | |
| P099 | Documentation | User manual, hardware build guide, API reference, developer guide, troubleshooting | Reviewed | |
| P100 | Release v1.0 | Tag, GitHub Release with artifacts, demo video, retrospective | Released | |

---

## 4. Order of work and dependencies on STRATIA

| When | CloudScope | Why |
|---|---|---|
| This week | **P001–P003** | Freeze legacy; start capturing B0268 data for STRATIA immediately |
| Until 16 Nov | P004–P018 at low intensity (docs, build, CI, simulators) | STRATIA's CVPR work has priority |
| From 17 Nov | Stages C → D → E (camera before control: a great camera app is useful even without a mount) | |
| After STRATIA P096 (ONNX export) | Stage F | Needs the real model + contract |
| Then | G → H → I → J → K | Autonomy needs control + AI; remote needs daemon |

Cross-repo tests: P065/P067 here are tested against STRATIA's exported reference model and ray-map generator, so the two repos can't drift apart silently.

## 5. System-level failure modes to design for

| Risk | Mitigation (phase) |
|---|---|
| Camera pointed at the Sun (sensor damage; garbage frames) | Keep-out cone in planner + firmware (P060); sun detection in frame stats (P024) |
| Servo runaway / mechanical damage | Soft limits, slew limits, watchdog → park (P051, P057, P060) |
| Link loss between host and controller | Heartbeat + firmware safe state (P050, P060) |
| Auto-exposure ruins data consistency for STRATIA | Locked exposure in logger and missions; exposure in sidecar (P003, P025, P076) |
| Clock drift corrupts sun position / CBH pairing | GPS/RTC/NTP time discipline, UTC everywhere (P013, P053) |
| Pi 5 thermal throttling under inference | INT8 model, frame skipping, active cooling (P066, P096) |
| Remote access exposure | No port forwarding (Tailscale), auth + TLS + roles (P087, P090) |
| Silent wrong AI overlays | Contract validation, OOD badge, overlays only when valid (P065, P068) |
| Scope creep / unfinished features | Gates D/E/F/G/H; must-have list from SRS; no placeholder UI |

## 6. Hardware shopping list (reference, finalized in P009)

See `docs/hardware/reference_designs.md` (P009) for the verified bills of materials per tier: ESP32-S3-WROOM-1 (non-octal-PSRAM) · PCA9685 · 2 × 270° digital servos (MG996R excluded: 159° travel, 55 °C rating) or NEMA 17 + TMC2209 + belt reduction for ≈0.1° · BNO085 · BME280 · DS3231 / NEO-M9N · 6 V ≥5 A servo rail from 12 V · 2–3 W dew heater · ASA + PMMA enclosure · Raspberry Pi 5 + Active Cooler + 27 W PSU · optional AI HAT+ (box only).
