# P014 — Test infrastructure

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Make tests cheap to write and hard to fool: shared helpers, Qt event-loop support, test data with known answers, and coverage measured in CI.

## Requirements covered
NFR-MNT-03 (core library line coverage ≥ 70 %, measured in CI), NFR-DATA-03 (synthetic data is labelled: documented at its source), support for every requirement verified by test (method T).

## Design notes
- **One framework, Catch2**, with Qt Test used as a library inside it (`QSignalSpy`, `QTest::qWaitFor`) instead of a second test runner. Test executables share a `main()` with a `QCoreApplication`, so signals, timers and queued calls work in tests.
- **Test support library** (`tests/support`): `TempWorkspace` (temporary folder with a non-ASCII name), `data_path()` for fixtures, `read_file()`, `wait_until()`. The three per-file copies of temporary-folder code from P013 were replaced by it.
- **Fixtures** (`tests/data`): three real sky photographs from the CCSN database (CC0 1.0, 400 × 400 JPEG, no EXIF; cirrus, cumulus, stratus), listed in `manifest.json` with size, SHA-256, source and licence. A test fails if a fixture is missing, changed, unlisted, larger than 100 KB or if the folder exceeds 1 MB. `CONTRIBUTING.md` now states this single exception to "never commit images".
- **Synthetic sky generator** (`cloudscope/sim/synthetic_sky.hpp`, in the core library because the simulated camera of P018 will use the same code): blue gradient, noise-shaped clouds with soft edges, optional Sun disc with glow, sensor noise; any size up to 8192². It returns the ground truth with the image: cloud mask and fraction, Sun centre and radius, and the exact number of saturated pixels (only the Sun disc ever reaches 255). Deterministic for a given spec and seed.
- **Coverage**: build option `CLOUDSCOPE_COVERAGE` (GCC), CI job *Coverage* on Debian 13, `tools/ci/coverage_report.py` (gcovr) with a per-file table, an HTML report as artifact and a failing exit below 70 % or when nothing was measured. The figure is attached to the commit as a notice, which `tools/ci/status.py` now prints.
- Developer guide: `docs/dev/testing.md`.

## Work log
1. Chose the fixtures: viewed candidate CCSN images, kept three without people or text; confirmed CC0 (verified in STRATIA P015 through the Harvard Dataverse API) and absence of EXIF.
2. Wrote the support library, moved `test_config.cpp` and `test_log.cpp` onto it, added Qt Test.
3. Wrote the synthetic sky generator and its tests.
4. Added the coverage option, script, CI job and seven tests for the script; ran the whole flow in the Debian container.
5. Extended the licence table (Qt Test sits next to the test executables).

## Verification
Warnings as errors; Debug and Release.

| Environment | Tests |
|---|---|
| Windows 11 x64, MSVC 19.44, Qt 6.9.3 | 95/95 · 95/95 |
| Debian 13 x64 (Docker), GCC 14.2, Qt 6.8.2 | 94/94 · 94/94 |
| Ubuntu 26.04 x64 (Docker), GCC 15.2, Qt 6.10.2 | 94/94 · 94/94 |
| Debian 13 arm64 | by CI (recorded in `PROJECT_STATE.md` after the push) |

15 new C++ tests and 7 new Python tests (CI scripts: 68). The new C++ tests establish:
- fixtures: every listed file exists with the stated size and SHA-256, decodes to 400 × 400 BGR, and nothing unlisted is in the folder;
- temporary workspace exists during a test and is gone afterwards; the Qt event loop runs inside a test (single-shot timer observed with `QSignalSpy`, queued call executed); `wait_until` returns after its timeout;
- synthetic sky: image and mask formats; same spec gives a bit-identical image, another seed a different one; cloud fraction within 0.2 percentage points of the request for 0, 10, 25, 50, 75, 90 and 100 %; cloud pixels brighter and less blue than clear sky; the Sun is a saturated disc of π r² pixels (±3 %) centred within half a pixel of the stated position, and nothing outside it saturates even with noise σ = 6 and 80 % cloud; glow strength falls with distance and ends at six radii; Sun partly or wholly outside the frame; measured noise σ within 0.3 of the requested 4.0; a 4656 × 3496 frame (B0268 full resolution) is generated; eight invalid specs refused.

Coverage, Debian 13 container, Debug:

```
File                                                    Lines   Branches
core/src/common/log.cpp                          269/296   90.9%     80.6%
core/src/common/config.cpp                       267/290   92.1%     81.1%
core/src/common/app_config.cpp                    63/68    92.6%     79.7%
core/src/common/json_schema.cpp                  294/311   94.5%     89.6%
core/src/common/error.cpp                         36/38    94.7%     95.2%
core/src/common/self_test.cpp                    127/133   95.5%     85.3%
core/src/common/build_info.cpp                    57/59    96.6%     91.4%
apps/info/main.cpp                               112/114   98.2%     89.6%
core/src/common/clock.cpp                        153/155   98.7%     88.3%
core/src/sim/synthetic_sky.cpp                    97/98    99.0%     96.0%
(headers and units.cpp: 100 %)
Coverage: core library 94.3 % of lines (1401/1486); with applications 94.6 % (1513/1600); required 70 %
```

The lines not executed are failure branches that need fault injection (a log file that stops accepting writes, a backup that cannot be created), the "unknown value" defaults of `switch` statements, and Qt's fatal-message path.

## Exit criteria
- [x] Coverage in CI: job *Coverage* added; local run of the identical commands gives 94.3 % for the core library (threshold 70 %). CI result for this commit checked after the push.

## Safety & failure-mode notes
- **A coverage job that measures nothing must fail**, otherwise a broken instrumentation would show as success: the script exits with an error when no core-library lines were measured, and a test covers that case. A first version also crashed on files without branches (gcovr reports "no value"); fixed and tested.
- **Coverage is not correctness.** 94 % of lines run says nothing about what is asserted. The guide says so; reviews look at assertions. The 70 % threshold is the requirement, not the target.
- **Synthetic images are not bit-identical across platforms** (OpenCV's interpolation differs between CPU architectures in the last bit): tests assert properties and ground truth, never pixel hashes. Within one build the generator is deterministic.
- **Fixtures are third-party photographs, not sky-camera frames**: they exercise decoding and statistics on real image content, but say nothing about the B0268's optics or noise. Camera-specific tests need recordings from the camera (Stage C, when it is connected).
- Memory: running three container builds at once exhausted the laptop's 16 GB and crashed Docker during P013; local Linux builds now run one at a time with 8 parallel compile jobs.

## Deviations & next phase
- "Qt Test" in the plan is used as a library within Catch2, not as a second framework: one runner, one report format, one way to list tests.
- "Recorded-frame fixtures": no B0268 recordings can be committed (privacy and repository rules, and the camera is not connected), so the fixtures are CC0 photographs plus the synthetic generator with ground truth.
- The synthetic sky generator was placed in the core library one phase early (it is the image source of the simulated camera in P018).
- Next: **P015 — Static analysis**.
