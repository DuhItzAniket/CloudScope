# P002 — Docs & phase protocol

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Put the development process into the repository: the plan, the phase and ADR templates, contribution rules, state tracking, licence and line-ending policy.

## Requirements covered
None (process phase).

## Design notes
- `docs/PLAN.md` is the plan from `STRATIA/plans/CloudScope_Implementation_Plan.md` (2026-10-04), with the push target corrected to `master`. The plan is versioned with the code from now on; changes to it go through phase docs.
- Phase docs: `docs/phases/P###-<slug>.md`; index in `docs/phases/README.md`.
- ADRs: `docs/adr/ADR-###-<slug>.md` from `docs/adr/TEMPLATE.md` (first real ADRs in P008).
- **Licence:** Apache-2.0 added as the default proposed on 2026-10-04. The owner can still change it; the final decision is recorded in ADR-010 (P008). The text was fetched from apache.org (SHA-256 `cfc7749b…bc523d30`, 202 lines).
- **Line endings:** the development machine has `core.autocrlf=true`, which produced LF/CRLF warnings in P001. `.gitattributes` now fixes LF in the repository for all text and CRLF for `.bat/.cmd/.ps1`, so files behave the same on Windows and the Raspberry Pi.

## Work log
1. Added `docs/PLAN.md`, `docs/phases/TEMPLATE.md`, `docs/phases/README.md`, `docs/adr/TEMPLATE.md`.
2. Added `CONTRIBUTING.md` (phase workflow, commit convention, quality bar, never-commit list).
3. Added `PROJECT_STATE.md` and `CHANGELOG.md`.
4. Added `LICENSE` (Apache-2.0) and `.gitattributes`.
5. Ran `git add --renormalize .` under the new rules (see Verification).

## Verification
- `docs/phases/P001-legacy-freeze.md` already follows the template's sections, so the template is retroactively satisfied for P001.
- `git add --renormalize .` staged no changes to existing files: every tracked file was already stored with LF in the repository (the P001 warnings concerned the working copy only). The staged set is exactly the 10 new files of this phase.

## Exit criteria
- [x] Template exists and P001 and P002 follow it.
- [x] Commit convention and `Phase-Status` trailer documented in `CONTRIBUTING.md`.
- [x] `PROJECT_STATE.md` and `CHANGELOG.md` exist and are current.

## Safety & failure-mode notes
None.

## Deviations & next phase
- The plan put the licence decision in P008; a default licence was added now so the repository is not "all rights reserved" in the meantime.
- Next: **P003 — Interim Sky Logger** (urgent: STRATIA needs B0268 data).
