# P004 — Use cases & personas

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Define who uses CloudScope, in which hardware configurations, and what they must be able to do, as the input to the SRS (P005).

## Requirements covered
Produces the source for all FR groups in P005.

## Design notes
- Four personas (researcher, operator, remote viewer, developer) with explicit "must never experience" lists, which become quality requirements.
- Four deployment configurations (D1 fixed laptop camera … D4 Arduino Uno lite) make the "laptop or Raspberry Pi, any microcontroller" goal concrete. The host always runs CloudScope; microcontrollers are peripherals.
- 17 use cases in a uniform format, each pointing to SRS requirement groups and to the plan stage that delivers it.

## Work log
1. Wrote `docs/requirements/use_cases.md`.
2. Fixed a stale sentence in the root README ("once P002 lands").

## Verification
- Every persona goal is covered by at least one use case (P1 → UC-03/04/05/09/13/14/17; P2 → UC-01/02/03/06/07/10/12; P3 → UC-10/11; P4 → UC-05/06/08/16/17).
- Every plan stage C–I has at least one use case mapped to it (table in section 4).

## Exit criteria
- [x] Personas, deployments and use cases documented.
- [x] Each use case references requirement groups and plan stages.

## Safety & failure-mode notes
UC-15 makes fault handling a first-class use case rather than an afterthought.

## Deviations & next phase
None. Next: **P005 — SRS**.
