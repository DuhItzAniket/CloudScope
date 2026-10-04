# Contributing to CloudScope

CloudScope is built phase by phase following [`docs/PLAN.md`](docs/PLAN.md) (100 phases, stages A–K). These rules apply to every change, human- or AI-written.

## Phase workflow

1. Copy [`docs/phases/TEMPLATE.md`](docs/phases/TEMPLATE.md) to `docs/phases/P###-<slug>.md` when the phase starts.
2. Implement only what the phase covers. Out-of-scope ideas go into the phase doc's "Deviations & next phase" section, not into the code.
3. Verify every exit criterion and record the evidence (commands, outputs, measurements, screenshots).
4. Update [`PROJECT_STATE.md`](PROJECT_STATE.md) and [`CHANGELOG.md`](CHANGELOG.md).
5. Commit and push:
   ```
   git commit -m "P###: <title>" -m "<summary of what changed and how it was verified>" -m "Phase-Status: DONE"
   git push origin master
   ```
   `Phase-Status` is `DONE`, `PARTIAL` (some exit criteria not met, documented why) or `BLOCKED` (cannot proceed; documented what is needed).
6. At the end of a stage: `git tag -a stage-<X>-complete -m "Stage <X> complete"` and `git push origin --tags`.

From P012 onward, a phase is complete only when CI is green for its commit: build and test locally (Windows, and Linux through the Docker image) before pushing, then check the result with `python tools/ci/status.py --wait` ([`docs/dev/ci.md`](docs/dev/ci.md)). A red commit is fixed by the next commit, never left.

## Quality bar

- **No placeholders that pretend to work.** Every visible control does something real and is covered by a test. Unfinished features are hidden or clearly labelled as unavailable.
- **No fabricated data.** Never present simulated, guessed or default values as measurements. Simulator output is always labelled as simulated.
- **Traceability.** Features trace to a requirement ID in the SRS (P005) and to an acceptance test (P097).
- **Safety in two places.** Anything that protects hardware or the camera (Sun keep-out, limits, watchdog) is enforced on the host *and* in firmware.
- **Honest phase docs.** Failures and partial results are written up with root cause and next action.

## Never commit

Captured images, datasets, model weights (`*.onnx`, `*.pth`, …) other than the two frozen legacy deliverables, credentials, tokens, keys, or personal photos. Large artefacts go to GitHub Releases.

The only exception is small, licence-clean test fixtures under the rules of [`tests/data/README.md`](tests/data/README.md) (CC0 or made by this project, no personal data or EXIF, at most 100 KB each, listed in a manifest with checksums).

## Line endings and encoding

UTF-8 everywhere. Line endings are normalised by [`.gitattributes`](.gitattributes): LF in the repository, CRLF only for Windows batch files.

## Coding standards

- **C++:** [`docs/dev/cpp_style.md`](docs/dev/cpp_style.md); build instructions in [`docs/dev/building.md`](docs/dev/building.md); tests in [`docs/dev/testing.md`](docs/dev/testing.md). Formatting and lint tooling follow in P015.
- **Python tools:** as in `tools/sky_logger/` (P003).
- **Firmware:** defined in P050.
