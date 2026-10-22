# Project State

| Field | Value |
|---|---|
| Current stage | Stage F — STRATIA integration (P065–P072) complete as of 2026-10-21 (Gate F: GO) for the AI-day demo of 2026-10-30; Stage E — Hardware control system (P047–P064) postponed at the owner's request (2026-10-16); Stage D complete (Gate D: GO); Stage C complete on the laptop (P032 PARTIAL); Stages A and B complete |
| Last completed phase | P072 — Gate F — AI pipeline |
| Next phase | AI-day demo (2026-10-30) with the B0268 and `models/stratia-1.0.0-demo`; then Stage E — P047 CSDP spec v1 when the owner returns to hardware; owner items of Stage C unchanged |
| Branch | `master` |
| Legacy prototype | `legacy/`, tag `v0.1-aiday` |
| Last CI result checked | `681858d` (Stage F follow-up 3, run #30): all ten jobs green (Debian 13 x64 and arm64, Ubuntu 26.04 with Qt 6.10, Windows x64, address/undefined and thread sanitizers, coverage, format and static analysis, secret scan, Python tools) after three follow-up fixes (15cef39 fixture line endings and GCC/clang-tidy strictness, bdb4e3a the dummy model fixture that `.gitignore` had kept out of the repository, 681858d GCC 14 Release warnings and the thread sanitizer with a loaded model) |
| Blockers | None for the code. Owner items from Stage C: daylight `ae-test` of the B0268 (P025), real dark/flat masters (P026), a FITS and a 16-bit SER opened in Siril/SER Player (P027/P028), a printed checkerboard for the B0268's intrinsic calibration (P031), the Linux/Raspberry Pi run of the V4L2 backend (P019, P032), EXIF GPS privacy in `legacy/B0268`, the B0268 logging run for STRATIA P041 |
| Owner decisions (Gate R) | Apache-2.0 · servo tier R1 first · laptop host for now · low intensity until 16 Nov 2026 (STRATIA has priority) |
| Hardware on the development machine | Arducam B0268 (`uvc:0c45:636d:1`) and the integrated laptop camera, both on USB; an ESP32-S3 with an MPU6500, a PCA9685 and two MG995 servos on the bench (not attached to CloudScope yet, Stage E); no Raspberry Pi; NVIDIA RTX 4050 Laptop GPU (DirectML; no CUDA toolkit) |

## Phase log

| Phase | Title | Status | Date |
|---|---|---|---|
| P001 | Legacy freeze | DONE | 2026-10-04 |
| P002 | Docs & phase protocol | DONE | 2026-10-04 |
| P003 | Interim Sky Logger | PARTIAL | 2026-10-04 |
| P004 | Use cases & personas | DONE | 2026-10-04 |
| P005 | Software requirements specification | DONE | 2026-10-04 |
| P006 | Competitive analysis | DONE | 2026-10-04 |
| P007 | System architecture | DONE | 2026-10-04 |
| P008 | Architecture decision records | DONE | 2026-10-04 |
| P009 | Hardware reference designs | DONE | 2026-10-04 |
| P010 | Gate R — requirements and architecture review | DONE | 2026-10-04 |
| P011 | Build system | DONE | 2026-10-04 |
| P012 | CI | DONE | 2026-10-04 |
| P013 | Core utilities | DONE | 2026-10-04 |
| P014 | Test infrastructure | DONE | 2026-10-04 |
| P015 | Static analysis | DONE | 2026-10-04 |
| P016 | Threading & event bus | DONE | 2026-10-04 |
| P017 | HAL interfaces | DONE | 2026-10-06 |
| P018 | Simulators | DONE | 2026-10-06 |
| P019 | Device enumeration | DONE | 2026-10-09 |
| P020 | Modes | DONE | 2026-10-09 |
| P021 | Camera controls | DONE | 2026-10-09 |
| P022 | Acquisition pipeline | DONE | 2026-10-09 |
| P023 | Decode & colour | DONE | 2026-10-09 |
| P024 | Frame statistics | DONE | 2026-10-09 |
| P025 | Sky auto-exposure + HDR | DONE | 2026-10-09 |
| P026 | Calibration frames | DONE | 2026-10-09 |
| P027 | Recording I | DONE | 2026-10-09 |
| P028 | Recording II | DONE | 2026-10-09 |
| P029 | Capture sequencer | DONE | 2026-10-09 |
| P030 | Sessions & catalogue | DONE | 2026-10-09 |
| P031 | Intrinsic calibration tool | DONE | 2026-10-09 |
| P032 | Camera soak test | PARTIAL | 2026-10-09 |
| P033 | App shell | DONE | 2026-10-12 |
| P034 | Live view | DONE | 2026-10-12 |
| P035 | Camera control panel | DONE | 2026-10-12 |
| P036 | Histogram, scopes & stretch | DONE | 2026-10-12 |
| P037 | Overlays | DONE | 2026-10-12 |
| P038 | Image tools | DONE | 2026-10-12 |
| P039 | Capture panel | DONE | 2026-10-12 |
| P040 | Session browser | DONE | 2026-10-12 |
| P041 | Preferences | DONE | 2026-10-12 |
| P042 | Console & status | DONE | 2026-10-12 |
| P043 | Input devices | DONE | 2026-10-12 |
| P044 | UX polish | DONE | 2026-10-12 |
| P045 | UI test automation | DONE | 2026-10-12 |
| P046 | Gate D — Workbench review | DONE | 2026-10-12 |
| P065 | Contract implementation | DONE | 2026-10-21 |
| P066 | Inference engine | DONE | 2026-10-21 |
| P067 | Metadata provider | DONE | 2026-10-21 |
| P068 | AI overlays | DONE | 2026-10-21 |
| P069 | AI logging & charts | DONE | 2026-10-21 |
| P070 | Inference benchmark tool | DONE | 2026-10-21 |
| P071 | Model manager | DONE | 2026-10-21 |
| P072 | Gate F — AI pipeline | DONE | 2026-10-21 |
