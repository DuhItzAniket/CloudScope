# Project State

| Field | Value |
|---|---|
| Current stage | Stage C — Camera subsystem (P019–P032) in progress: P019–P031 done on 2026-10-09 with the Arducam B0268 on the development laptop; P032 (soak) running. Stages A and B complete (tag `stage-B-complete`) |
| Last completed phase | P031 — Intrinsic calibration tool |
| Next phase | P032 — Camera soak test |
| Branch | `master` |
| Legacy prototype | `legacy/`, tag `v0.1-aiday` |
| Last CI result checked | `7f606a2` (P018, tag `stage-B-complete`): all ten jobs green; coverage 96.3 % of core-library lines. (The two runs before it lost jobs to GitHub's Actions incident of 5 Oct 2026, not to failures.) |
| Blockers | None for the code. Owner items from Stage C: daylight `ae-test` of the B0268 (P025), real dark/flat masters (P026), a FITS and a 16-bit SER opened in Siril/SER Player (P027/P028), a printed checkerboard for the B0268's intrinsic calibration (P031), the Linux/Raspberry Pi run of the V4L2 backend (P019, P032), EXIF GPS privacy in `legacy/B0268`, the B0268 logging run for STRATIA P041 |
| Owner decisions (Gate R) | Apache-2.0 · servo tier R1 first · laptop host for now · low intensity until 16 Nov 2026 (STRATIA has priority) |
| Hardware on the development machine | Arducam B0268 (`uvc:0c45:636d:1`) and the integrated laptop camera, both on USB; no microcontroller attached; no Raspberry Pi |

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
