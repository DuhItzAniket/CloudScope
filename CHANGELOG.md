# Changelog

All notable changes, grouped by phase. Versions follow [Semantic Versioning](https://semver.org/); the system reboot targets v1.0.0 at P100.

## [Unreleased]

### P016 — Threading & event bus (2026-10-04)
- Added the concurrency building blocks: `SpscQueue` (lock-free), `ThreadPool` and executors (inline, Qt), `Signal` with blocking disconnect, `EventBus`.
- Added the frame path: `Frame`, `FramePool` (buffers allocated once and reused), `FrameHub` with per-consumer `Queue` (every frame, counted drops) or `Latest` (newest only) delivery.
- Added `cloudscope-bench` and a benchmark test: 4K BGR frames are copied and handed over at 794 fps on Windows and 679 fps on Linux on the laptop without a dropped frame (required: more than 60 fps); enforced in CI in Release builds.
- Added the thread-sanitizer CI job; all tests pass under the thread, address and undefined-behaviour sanitizers. 46 new tests (142 on Windows, 141 on Linux, plus the benchmark test in Release).
- Added `docs/dev/threading.md`.

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
