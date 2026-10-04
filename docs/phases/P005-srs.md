# P005 — Software requirements specification

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Turn the use cases (P004) into numbered, prioritised, verifiable requirements with a delivering phase, so that every later phase and the final acceptance test (P097) can be traced.

## Requirements covered
Defines them: `docs/requirements/SRS.md`.

## Design notes
- 15 functional groups (CAM, DSP, REC, SEQ, CAL, CTL, FW, SAF, MOD, MIS, AI, REM, SEC, DAT, PLT) and 7 non-functional groups (PERF, REL, SAFE, SEC, USE, MNT, DATA).
- MoSCoW priorities; verification by test, demonstration, inspection or analysis; one acceptance test `AT-<ID>` per requirement.
- **Safety is specified as a two-layer system:** the host computes the Sun keep-out zone and soft limits, the controller enforces the last zone it received, and the zone *expires* after 120 s without refresh so a crashed host cannot leave the controller with stale permissions (FR-SAF-01…05).
- **Honesty requirements are explicit:** read-back of every camera setting (FR-CAM-04), measured vs declared metadata (NFR-DATA-02), simulated data always labelled (NFR-DATA-03), no dead controls (NFR-USE-01), uncertain AI output marked as such (FR-AI-06).
- Performance targets that depend on unmeasured hardware (B0268 modes, Pi 5 throughput, STRATIA latency) are stated as targets and verified by analysis in their phases; open questions are listed with the phase that resolves them.

## Work log
1. Wrote `docs/requirements/SRS.md` (138 requirements).
2. Verified the summary table by parsing the document; the first hand count had three errors (FR-CTL, FR-MIS, NFR), corrected.

## Verification
- Parser over the SRS tables: 138 requirement rows, 0 duplicate IDs; per-group counts match the summary table (M 104, S 28, C 6).
- Every use case UC-01…UC-17 is referenced by at least one requirement group, and every requirement names a phase that exists in `docs/PLAN.md`.

## Exit criteria
- [x] Every FR has an ID and a verification method (an acceptance test idea).
- [x] NFRs cover performance, reliability, safety, security, usability, maintainability and data integrity.
- [x] Traceability rule defined (generated `traceability.csv` from P012).

## Safety & failure-mode notes
FR-SAF-02 (expiring keep-out zone in firmware) and NFR-SAFE-02 (fault-injection test for every safety function on simulator and hardware) are the key safety commitments.

## Deviations & next phase
None. Next: **P006 — Competitive analysis** (research running in parallel).
