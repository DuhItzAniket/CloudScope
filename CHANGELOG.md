# Changelog

All notable changes, grouped by phase. Versions follow [Semantic Versioning](https://semver.org/); the system reboot targets v1.0.0 at P100.

## [Unreleased]

### P031 — Intrinsic calibration tool (2026-10-09)
- Added intrinsic calibration (`calibration/intrinsics.hpp`): checkerboard detection, a capture assistant that keeps the views that cover new parts of the image, OpenCV fisheye and pinhole fits, reprojection error, pixel ↔ ray, and the camera-model file (schema `cloudscope.camera_model/1`) that STRATIA's camera models read.
- `cloudscope-camtool calibrate` runs the assistant on a live camera or on a folder of pictures. OpenCV's `calib3d` module joins the dependencies.

### P030 — Sessions & catalogue (2026-10-09)
- Added sessions (`session/session.hpp`): the folder layout `<root>/<site>/<date>/<session-id>/{frames,calibration,logs}` with a manifest (schema `cloudscope.session/1`) rewritten atomically as pictures arrive.
- Added the frame catalogue (`session/catalogue.hpp`): an SQLite database (Qt SQL, QSQLITE) of sessions and frames with time, file, hash, exposure, statistics and Sun columns; time-range and newest-first queries, indexing of folders from sidecars, and a retention policy by size or age that removes the oldest pictures with their sidecars.
- Qt Sql joins the dependencies (`libqt6sql6-sqlite` on Debian; the plugin is copied next to the executables on Windows).
- Test executables report C-runtime assertions on stderr and abort instead of showing a dialog that hangs the test.

### P029 — Capture sequencer (2026-10-09)
- Added the capture sequencer (`capture/sequencer.hpp`): single pictures, bursts, fixed-rate interval series (missed slots skipped and counted, never caught up in a burst), exposure brackets and scheduled runs; day and night profiles switched at a configurable Sun elevation; pause and resume on Sun elevation; stop on count, end time or duration; a disk guard; recovery of a failed camera with exponential backoff; file names from a template (`{site}_{utc}_{seq}_{profile}` by default).
- `cloudscope-camtool record` and `sequence` run plans against any camera, with `--site` for the Sun position and `--session ROOT` to record into a catalogued session; manual in `docs/manual/camera_tool.md`.
- The 1,000-frame interval run of the exit criterion is a hidden `[slow]` test (about a minute in Debug); a 100-frame twin runs in the default suite.

### P028 — Recording II (2026-10-09)
- Added SER video (`capture/video_files.hpp`): writer and reader for 8-bit mono, 16-bit mono and 8-bit BGR with the version-3 UTC timestamp trailer; the header's endianness field follows the convention Siril, SER Player and FireCapture use (0 = little-endian data), verified against Siril's source.
- Added time-lapse assembly from a folder of pictures, keograms and star trails. MP4/H.264 is not built (FFmpeg is not a dependency).
- Added the Sun position (`capture/solar.hpp`, NOAA algorithm, checked against NREL SPA to 0.05°) and the twilight periods for the sequencer's day/night switch.

### P027 — Recording I (2026-10-09)
- Added picture recording (`capture/recording.hpp`): PNG, 16-bit TIFF, JPEG (including MJPEG frames stored as the camera sent them) and FITS with the agreed keyword set (DATE-OBS, TIMESYS, MJD-OBS, EXPTIME, GAIN, OBSGEO-B/L/H, SITELAT/SITELONG/SITEELEV, CENTALT/CENTAZ, ROWORDER BOTTOM-UP, SWCREATE, calibration id); every file goes through a temporary name and carries its SHA-256.
- Added the frame sidecar, schema `cloudscope.frame/1` (`core/resources/sidecar.schema.json`): file, capture time and source, camera and controls as read back, site, pointing with its source, Sun, statistics, session and software; `read_sidecar()` also reads the interim logger's `cloudscope.sky_logger.frame/1`.
- FITS files are produced through cfitsio memory files, because cfitsio cannot open every folder name by itself. Not implemented: the AstroTIFF header (FR-REC-10) and WCS keywords (FR-REC-12, with P031).

### P026 — Calibration frames (2026-10-09)
- Added calibration frames (`capture/calibration_frames.hpp`): dark and flat masters from stacks of frames, a gain map, the correction, a radial vignetting fit and a uniformity measure; masters are saved as 16-bit TIFF with a JSON sidecar; `cloudscope-camtool dark|flat` capture them.
- Image files are read and written through `std::filesystem` paths, so that folders with non-ASCII names work on Windows.

### P025 — Sky auto-exposure + HDR (2026-10-09)
- Added the sky exposure controller (`capture/exposure.hpp`): meters the 99th percentile of luma outside the Sun's disc towards a target with a dead band and a bounded step, moves exposure before gain; exposure brackets in stops and an exposure-fusion (Mertens) merge without OpenCV's photo module.
- `cloudscope-camtool ae-test` compares the camera's automatic exposure with the sky controller; the B0268 run of this phase saw a dark scene, so the daylight comparison is still open.

### P024 — Frame statistics (2026-10-09)
- Added frame statistics (`capture/statistics.hpp`): histogram, mean, median, percentiles, clipped and dark fractions, noise estimate, sharpness, and a Sun finder (the largest saturated disc with its position, radius and fill); `saturation_map()` for the display's over-exposure highlight.

### P023 — Decode & colour (2026-10-09)
- Added frame decoding (`capture/decode.hpp`): MJPEG through TurboJPEG (one handle per thread), YUYV, RGB/BGR and grey 8/16-bit into OpenCV images; JPEG headers are read without decoding.
- `cloudscope-camtool decode-bench`: 1080p MJPEG decodes in 5.7 ms (median, Debug) on the development laptop.

### P022 — Acquisition pipeline (2026-10-09)
- Added the acquisition thread (`capture/acquisition.hpp`): reads frames into pooled buffers, stamps and publishes them through the `FrameHub`, counts lost frames, pool misses and timeouts, and keeps the error that ended a stream; never waits for a consumer.
- `cloudscope-camtool stream`: on the B0268, 1080p MJPEG runs at 28 fps with zero lost frames and sub-millisecond publication latency; 16 MP at 9.6 fps.

### P021 — Camera controls (2026-10-09)
- Camera controls: exposure, gain, white balance and brightness through DirectShow's camera-control and video-processing-amplifier interfaces, with range, step, default and automatic mode discovered from the driver and every setting read back (`ControlState{requested, effective, applied}`); exposure is converted between the driver's log2-seconds scale and milliseconds.
- `cloudscope-camtool set` and `exposure-test`: the B0268's exposure control responds monotonically over 1–128 ms (table in `docs/hardware/b0268_measured.md`); its gain scale is uncalibrated and reported as such.

### P020 — Modes (2026-10-09)
- Camera modes: the UVC driver lists every size, pixel format and rate the camera offers (MJPEG, YUYV, RGB24), `set_mode()` returns the mode in effect and refuses a mode while streaming; `cloudscope-camtool measure` streams each mode and reports the delivered rate and losses.
- Measured on the Arducam B0268 (`docs/hardware/b0268_measured.md`): MJPEG modes deliver 28.1 fps at 1080p and 9.7 fps at 16 MP with no lost frames; YUYV modes deliver about 75 % of their nominal rate and lose frames at the small sizes.

### P019 — Device enumeration (2026-10-09)
- Added the UVC camera driver (`core/include/cloudscope/uvc/`, driver `uvc`): enumeration of USB Video Class cameras with stable ids `uvc:<vendor>:<product>:<n>`, a Media Foundation backend on Windows (one asynchronous source reader per camera, newest-frames sink, drop detection from sample timestamps) and a V4L2 backend on Linux (memory-mapped streaming; compiled in CI, not yet run on hardware).
- Added the `[camera]` configuration section (`uvc = true`); `add_configured_drivers()` lists real cameras before simulated ones, and `cloudscope-info --devices` shows both.
- Added `cloudscope-camtool` (`apps/camtool`), the camera subsystem from the command line: `list`, `caps`, and the commands of the following phases.
- The Arducam B0268 (16 MP, `uvc:0c45:636d:1`) and the laptop's camera enumerate; the B0268 passes the full HAL camera contract (819 assertions) in the hidden `[hardware]` tests.

### P018 — Simulators (2026-10-06)
- Added simulated devices behind the hardware interfaces (`core/include/cloudscope/sim/`, driver `sim`): a sky camera with drifting clouds, a Sun, an exposure, gain, white-balance and noise model, automatic exposure and 11 modes in six pixel formats; a replay camera for a folder of pictures; a pan-tilt mount with speed and acceleration limits, command and telemetry latency, optional position feedback with noise and an emergency stop; an IMU; a GPS receiver; environment sensors. All share one simulated rig, so the IMU turns when the mount moves.
- Simulated devices run on any clock: in real time on the system clock, instantly and repeatably on a manual clock. Tests can stall an axis, cut the controller link, pull the camera's cable, lose frames, take the GPS fix away or make it rain.
- Every simulated device passes the contract tests of P017; a whole rig (camera through the frame hub to two consumers while the mount moves) runs end to end without hardware.
- Added the `[simulation]` configuration section (`enabled`, `seed`, `replay_folder`, `replay_fps`) and `cloudscope-info --devices`, which lists the usable devices and marks simulated ones.
- Added `AxisProfile` (motion of one axis from any state to rest at a target) and quaternion and pointing mathematics (`geometry/rotation.hpp`).
- 67 new tests (232 on Windows, 231 on Linux, plus the benchmark test in Release); all pass under the address, undefined-behaviour and thread sanitizers.
- Added `docs/dev/simulators.md` and the manual section *Simulated devices*; the static-analysis CI job may now run for up to 120 minutes.
- Stage B (engineering foundation) is complete.

### P017 — HAL interfaces (2026-10-06)
- Added the hardware abstraction layer (`core/include/cloudscope/hal/`): `ICamera`, `IMount`, `IImu`, `ISensor`, `ITransport`, `IInference`, each with a capability description discovered at run time, and a thread-safe `DeviceRegistry` with drivers (`IDriver`) and stable device ids (`<driver>:<rest>`).
- Cameras are pulled (`read_frame`) rather than pushing from a thread of their own; setting a mode or control returns what is really in effect; devices state whether values are calibrated, positions measured, and data simulated.
- Added contract tests (`tests/contract/`): the rules of each interface as test functions that every implementation runs.
- Added mock devices and a mock driver (`tests/support/mock_devices.hpp`) that pass the contracts and can lose frames, fall silent, be unplugged or report a fault on request.
- 23 new tests (165 on Windows, 164 on Linux, plus the benchmark test in Release); all pass under the address, undefined-behaviour and thread sanitizers.
- Added `docs/dev/hal.md`; testing guide, style guide, architecture document and README updated.

### P016 — Threading & event bus (2026-10-04)
- Added the concurrency building blocks: `SpscQueue` (lock-free), `ThreadPool` and executors (inline, Qt), `Signal` with blocking disconnect, `EventBus`.
- Added the frame path: `Frame`, `FramePool` (buffers allocated once and reused), `FrameHub` with per-consumer `Queue` (every frame, counted drops) or `Latest` (newest only) delivery.
- Added `cloudscope-bench` and a benchmark test: 4K BGR frames are copied and handed over at 794 fps on Windows and 679 fps on Linux on the laptop without a dropped frame (required: more than 60 fps); enforced in CI in Release builds.
- Added the thread-sanitizer CI job; all tests pass under the thread, address and undefined-behaviour sanitizers. 46 new tests (142 on Windows, 141 on Linux, plus the benchmark test in Release).
- Added `docs/dev/threading.md`.
- Fixed after the first CI run: a GCC 15 warning-as-error in the queue (Ubuntu 26.04, Release) and a timed wait that could return slightly early (Windows).

### P015 — Static analysis (2026-10-04)
- Added formatting (`.clang-format`, `tools/dev/format.py`) and static analysis (`.clang-tidy`, `tools/dev/tidy.py`) with every finding an error; the code base was brought from 100 clang-tidy findings to zero.
- Added the sanitizer build option `CLOUDSCOPE_SANITIZE`; all tests pass under AddressSanitizer, the leak detector and UndefinedBehaviorSanitizer.
- Added three CI jobs: Format and static analysis, Sanitizers, Secret scan (gitleaks over the whole history).
- Code changes from the findings: `cloudscope-info` catches every exception in `main()` (exit code 3) and reports a failed write; designated initialisers for structs; NaN-safe range checks; new `ScopeExit` helper; one-byte enums; ranges algorithms.
- Added `docs/dev/quality_gates.md`; style guide and CI guide updated.

### P014 — Test infrastructure (2026-10-04)
- Added the test support library (`tests/support`): temporary workspaces, fixture lookup, Qt event-loop waiting with Qt Test inside Catch2; existing tests moved onto it.
- Added test fixtures (`tests/data`): three CC0 sky photographs with a checksum manifest and rules, enforced by a test.
- Added the synthetic sky generator (`cloudscope/sim/synthetic_sky.hpp`) with ground truth: cloud mask and fraction, Sun position and radius, exact count of saturated pixels.
- Added coverage measurement: build option `CLOUDSCOPE_COVERAGE`, CI job *Coverage* (fails below 70 % line coverage of the core library, NFR-MNT-03), HTML report as artifact; `tools/ci/status.py` prints the figure. Local result: 94.3 % of core-library lines.
- 15 new C++ tests (95 on Windows, 94 on Linux) and 7 new Python tests; developer guide `docs/dev/testing.md`.

### P013 — Core utilities (2026-10-04)
- Added core utilities (`core/include/cloudscope/common/`): `Expected<T>` error returns, `Degrees`/`Radians` strong types, clocks and ISO 8601 UTC timestamps with time source, logging (rotating file, in-memory buffer with listeners, secret redaction, Qt message routing), a JSON Schema subset validator with user-oriented messages, and layered TOML configuration with schema validation, automatic migration and backups.
- Added CloudScope's `config.toml` format (version 1: `[logging]`), embedded schema and defaults, standard file locations; `cloudscope-info --show-config` and `--config FILE`; user documentation `docs/manual/configuration.md`.
- 65 new tests (80 on Windows, 79 on Linux); verified with warnings as errors on Windows, Debian 13 x64 and arm64, Ubuntu 26.04.
- New dependency tl-expected (CC0-1.0) in vcpkg, the Debian package list and the licence table. ADR-007 gains implementation notes (own validator and file sink, and why).
- Found and fixed: a recursive template that exhausted MSVC's memory; test names with square brackets that made CMake 3.31 silently merge tests.

### P012 — CI (2026-10-04)
- Added CI (`.github/workflows/ci.yml`): Debian 13 x64, Debian 13 arm64, Ubuntu 26.04 x64, Windows x64 (MSVC, vcpkg, Qt 6.8.3 via aqtinstall) and Python tools; Debug and Release builds with warnings as errors, tests, licence gate, Release binaries as artifacts. First run green on all five jobs.
- Added `tools/ci/`: `run.py` (failure output attached to the commit as annotations), `status.py` (CI result and failure details from the laptop without a GitHub sign-in), `licence_check.py` (ADR-010 gate against `packaging/licences/third_party.toml`), with 61 tests.
- Verified the failure path with a temporary branch (compile error on Linux, failing test on Windows) and the Windows caches (configure 47 s instead of 18.6 min).
- ADR-010 amended: exact licence allow-list in the component table; compiler and C runtime as "system" libraries. `docs/dev/ci.md` added; CI badge in the README.

### P011 — Build system (2026-10-04)
- Added the build system: CMake presets `windows-msvc`, `linux-x64`, `linux-aarch64` (Ninja Multi-Config), vcpkg manifest pinned to release 2026.07.29 for Windows, Debian package list for Linux and Raspberry Pi OS, build scripts and a Docker build image (`tools/build/`).
- Added the first core module (`build_info`, `self_test`) and the `cloudscope-info` executable: version, git revision, compiler, library versions and licences, plus a self-test in which every bundled library does real work.
- Added unit and command-line tests (Catch2): 15 on Windows, 14 on Linux; verified with warnings as errors on Windows (MSVC 19.44, Qt 6.9.3), Debian 13 x64 and arm64 (GCC 14, Qt 6.8.2) and Ubuntu 26.04 (GCC 15, Qt 6.10.2).
- Fixed a hang found during verification: Qt's `QCommandLineParser::process()` opens a message box on Windows when there is no console; executables now parse arguments themselves, with a regression test.
- Added developer guides `docs/dev/building.md` and `docs/dev/cpp_style.md`. SRS v1.4: FR-PLT-01 names Debian 13 and Ubuntu 26.04 instead of Ubuntu 24.04 (Qt 6.4 is too old). `.gitignore` build patterns anchored to the repository root.

### P010 — Gate R — requirements and architecture review (2026-10-04)
- Added the Gate R review record (`docs/reviews/gate_R.md`): 11-item checklist, automated consistency checks, three findings fixed (stale Qt HttpServer in the architecture, missing requirement-group traceability, resolved open questions), owner decisions, system risk register.
- Verdict: GO for Stage B with conditions. Stage A tagged `stage-A-complete`.

### P009 — Hardware reference designs (2026-10-04)
- Added verified component facts (`docs/research/P009_hardware_component_facts.md`, 66 sources) and hardware reference designs (`docs/hardware/reference_designs.md`): split sky-head/electronics-box layout, servo and stepper pan-tilt designs, Pi 5 box and native GPIO tier, Uno R4/R3 lite tier, power distribution, thermal plan for Bengaluru.
- Design changes from verified facts: MG996R excluded (159° travel, 55 °C), AI Kit replaced by AI HAT+ (box only), BNO085 and ESP32-S3 as references, PPS moved to GPIO17; SRS v1.3.

### P008 — Architecture decision records (2026-10-04)
- Added ADR-001…012 and an ADR index: C++20/Qt ≥ 6.8, Qt Widgets + ADS, one Session API, ONNX Runtime, CSDP, Drogon, TOML + spdlog, image formats, PlatformIO, licensing policy, ASCOM Alpaca/INDI, shared time and coordinate conventions.
- Changed two plan choices after verifying primary sources: Drogon replaces the GPL-only Qt HTTP Server; the Pi platform is Raspberry Pi OS on Debian 13 "Trixie" (Qt 6.8.2) instead of Bookworm (Qt 6.4) — SRS v1.2.

### P007 — System architecture (2026-10-04)
- Added `docs/arch/architecture.md`: C4 context, container and component views, the single Session API with local and remote implementations, thread model, capture and safety sequences, failure handling, and the time and coordinate conventions shared with STRATIA (six Mermaid diagrams, render-checked).

### P006 — Competitive analysis (2026-10-04)
- Added the competitive analysis: evidence document with product profiles, feature matrix and format facts (98 sources) and a decision summary (D1–D7).
- SRS v1.1: 10 new requirements (raw/ROI/binning, preview-only stretch, AstroTIFF, keograms and star trails, FITS WCS, day/night profiles, ASCOM Alpaca client and server, INDI client, MQTT + Home Assistant) and a tightened FITS keyword requirement; 148 requirements in total.
- Plan rows P020, P027, P028, P029, P036, P055, P084, P085 updated accordingly.

### P005 — Software requirements specification (2026-10-04)
- Added `docs/requirements/SRS.md`: 138 requirements (104 must, 28 should, 6 could) across 15 functional and 7 non-functional groups, each with priority, verification method and delivering phase.

### P004 — Use cases & personas (2026-10-04)
- Added `docs/requirements/use_cases.md`: 4 personas, 4 deployment configurations, 17 use cases mapped to plan stages.

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
