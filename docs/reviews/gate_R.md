# Gate R — Requirements and architecture review

Date: 2026-10-04 · Phase: P010 · Scope: Stage A (P001–P009) · Reviewer: Claude (automated checks + document review), pending owner sign-off on the decisions in §4

## 1. Checklist

| # | Check | Result | Evidence |
|---|---|---|---|
| 1 | Personas and deployments cover the owner's goals (laptop or Pi; any microcontroller; manual and autonomous; remote; SharpCap-class app) | Pass | `use_cases.md` §1–§3 |
| 2 | Every use case maps to requirement groups and plan stages | Pass | `use_cases.md` §4; script: all 15 FR groups referenced by use cases |
| 3 | Requirements are uniquely identified, prioritised and verifiable | Pass | 148 requirements, 0 duplicate IDs, each with MoSCoW priority and T/D/I/A method |
| 4 | Every requirement names an existing plan phase | Pass | Script: 0 requirements point to a missing phase; plan has exactly 100 phases |
| 5 | Architecture covers every requirement group | Pass after fix | Script initially flagged FR-DSP, FR-FW, FR-SEC as not named; container→group mapping added |
| 6 | ADRs are consistent with the SRS, plan and architecture | Pass after fix | `architecture.md` still named "Qt HttpServer" (written before ADR-006); corrected to Drogon |
| 7 | No stale decisions in living documents | Pass | Script found 9 term matches; 8 are deliberate historical mentions (legacy QML, revision history, "AI Kit is discontinued", logger runs on Bookworm), 1 was the defect in #6 |
| 8 | Hardware designs respect SRS constraints and safety requirements | Pass | `reference_designs.md` §1, §8, §9; SRS C6/C7; NFR-SAFE-03 |
| 9 | Licences of chosen dependencies are compatible with the project licence | Pass | ADR-010 with verified Qt module licences; Drogon MIT; ADS LGPL-2.1 |
| 10 | Open questions are tracked with owning phases | Pass | SRS §5: Q2 and Q3 resolved, Q4 narrowed, Q1 open (P060) |
| 11 | Every phase P001–P009 has a phase document, a commit and a push | Pass | `docs/phases/`, commits 8e51f66 … bfc32f4 |

## 2. Findings fixed during the review

| # | Finding | Fix |
|---|---|---|
| F1 | `architecture.md` listed Qt HttpServer for the daemon (contradicts ADR-006 and the licence policy) | Container table updated to Drogon |
| F2 | Requirement groups FR-DSP, FR-FW, FR-SEC not traceable to an architecture container | Container→requirement-group mapping added to `architecture.md` §2 |
| F3 | SRS open questions Q2 (servo vs stepper) and Q4 (accelerator) answered by P009 but still open | Marked resolved / narrowed |

## 3. Observations carried forward

| # | Observation | Action |
|---|---|---|
| O1 | Phases P027 (recording, 10 requirements) and P060 (safety, 10) carry the most requirements | Allowed to split into sub-phases (P027a/b, P060a/b) at execution time, each with its own phase document |
| O2 | P003 (sky logger) is PARTIAL: B0268 24-hour run, Raspberry Pi test and STRATIA ingest outstanding | Completed when the B0268 is connected; tracked in `PROJECT_STATE.md` |
| O3 | Pin maps (ESP32-S3, Pi 5) are provisional | Confirmed in P050 and P055 |
| O4 | SRS open question Q1 (default Sun keep-out half-angle for a wide lens) | Decided in P060 with measurements |

## 4. Decisions needed from the owner

| # | Decision | Recommendation | Blocks |
|---|---|---|---|
| D1 | Confirm the licence | Apache-2.0 (already in place) | Nothing until first release |
| D2 | Hardware tier to buy | R1 servo pan-tilt with ESP32-S3 first (cheaper, ≈ 0.5–1°); R2 steppers only if ≈ 0.1° pointing is needed | Stage E (P047+) |
| D3 | Host for field use | Laptop now; Raspberry Pi 5 + 27 W PSU when unattended operation is needed | Stage H deployment, P098 |
| D4 | Pace until 16 Nov 2026 | Keep CloudScope at low intensity (Stage B foundation only) so STRATIA's CVPR work has priority; connect the B0268 and run the sky logger now | — |

## 5. Risk register (system level)

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| K1 | Scope (100 phases, one developer) | High | High | Gates per stage; camera app (Stages C–D) is useful on its own before hardware and autonomy |
| K2 | Hardware not yet in hand | High | Medium | Simulator-first (P018); hardware phases marked HW and verified on arrival |
| K3 | STRATIA model not ready when Stage F starts | Medium | Medium | Contract-conformant dummy model for integration tests |
| K4 | Build complexity on Windows + Pi (Qt 6.8, ORT, Drogon) | Medium | Medium | arm64 CI from P012; Drogon spike in P084 with Boost.Beast fallback |
| K5 | Heat in the sky head (Bengaluru sun) | Medium | High | Split design, rated parts only in the head, measured in P094 before unattended use |
| K6 | Unsafe motion (Sun, limits, link loss) | Low | High | Two-layer safety with expiring keep-out zone; fault-injection tests (NFR-SAFE-02) |
| K7 | AI-written code quality | Medium | High | CI, static analysis, tests per phase, honest phase documents, no placeholder UI (NFR-USE-01) |
| K8 | Competing time with STRATIA's deadline | High | Medium | D4 pacing |

## 6. Verdict

**GO for Stage B (engineering foundation, P011–P018)**, with the conditions: owner confirms D1–D4; observations O1–O4 stay tracked. Stage A is tagged `stage-A-complete`.
