# P001 — Legacy freeze

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Preserve the AI-Day prototype exactly as it was, then clear the repository root for the CloudScope system reboot without losing any history.

## Requirements covered
None yet (the SRS arrives in P005). This phase is a prerequisite for every later phase.

## Design notes
- Annotated tag `v0.1-aiday` on commit `6bc2f85` ("SharpCap-style workbench: menus, tabs, camera knobs, histogram, reticle"), the last commit of the prototype.
- Everything was moved with `git mv` into `legacy/` so per-file history survives (`git log --follow legacy/<path>`).
- `.github/history/` (the 40 prototype phase reports) moved to `legacy/history/`, because new phase docs live in `docs/phases/`.
- Relative paths inside the legacy code are unchanged, so it still runs with `legacy/` as the working directory (checked: the `.bat` build scripts only use `%~dp0`-relative paths; the Python scripts use repository-relative `data/`, `models/` paths).
- The default branch is `master` (not `main`); all phases push to `master`.

## Work log
1. `git clone` (full history, 43 commits) into `C:/Users/Luikz/Downloads/STRATIA/repos/CloudScope`.
2. `git tag -a v0.1-aiday 6bc2f85`.
3. `git mv` of 14 top-level items and `.github/history` into `legacy/` (195 renames, 0 content changes).
4. Added a "frozen legacy" banner at the top of `legacy/README.md` (the only edited legacy file).
5. New root `README.md` describing the target system and the reboot.
6. New root `.gitignore` for the future layout (CMake/Qt build output, PlatformIO, Python venvs, captures, weights, secrets). The two legacy ONNX deliverables stay tracked through explicit negation rules.

## Verification
- `git status --short` before committing: 195 `R` (rename) entries plus the new/edited files listed above; no deletions.
- `git ls-files legacy | grep -E '\.(dll|exe|bin|pth)$'` returns nothing, so no binaries were tracked that the new ignore rules would conflict with.
- `git tag` shows `v0.1-aiday`; it is pushed together with this commit.

## Exit criteria
- [x] Tag `v0.1-aiday` exists and is pushed.
- [x] All prototype content is under `legacy/` with history preserved.
- [x] Root README explains the reboot and links the legacy tag.

## Safety & failure-mode notes
None (no hardware involved).

## Deviations & next phase
- Plan said "push to main"; the repository uses `master`. The plan is applied to `master`.
- Next: **P002 — docs and phase protocol**.
