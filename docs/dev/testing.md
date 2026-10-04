# Testing

## Running the tests

```
tools\build\build.bat                      (Windows: configure, build, test)
sh tools/build/build.sh                    (Linux)
ctest --preset windows-msvc -C Debug -R "unit.config"     (a subset, by name)
build\windows-msvc\bin\Debug\cloudscope-unit-tests.exe "[log]"   (one executable, by tag)
```

Every Catch2 test case is one CTest entry, named `<tier>.<test name>` and run in its own process with a 60 s timeout.

| Tier | Executable | What it tests |
|---|---|---|
| `unit.` | `cloudscope-unit-tests` | Functions and classes of the core library, in-process |
| `cli.` | `cloudscope-cli-tests` | The real executables, started as child processes: output, exit codes, files |

Later tiers (hardware-in-the-loop, user interface) get their own executables in their phases.

## Writing a test

- Framework: **Catch2 v3**. Test names are plain sentences that state the behaviour ("an existing backup is never overwritten"); no square brackets or semicolons in names. Tags in the second argument: `[component][topic]`.
- A test checks behaviour through the public interface, with real libraries and real files in a temporary folder. Assert exact values where the result is defined (messages, timestamps), ranges where it is physical or statistical.
- Every bug fix comes with a test that fails without the fix. Check that it really fails (as done for the no-console hang in P011).
- Table-driven cases use `GENERATE(table<...>(...))`; put the input in `CAPTURE` so a failure names its row.
- Helpers in `tests/support/test_support.hpp`:
  - `TempWorkspace`: a folder for one test, deleted afterwards; its name is deliberately not ASCII.
  - `data_path("sky/ccsn_cu_n001.jpg")`: a fixture from `tests/data`.
  - `wait_until(condition, timeout)`: runs the Qt event loop until the condition holds; with `QSignalSpy` (Qt Test) this covers signals, timers and queued calls. Test executables have a `QCoreApplication`.
  - `ManualClock` (core, `clock.hpp`) wherever code takes an `IClock&`: tests never sleep to make time pass.
- Tests that start the application redirect its configuration folders into a temporary home (see `Home` in `tests/integration/test_cli_info.cpp`), so a developer's own settings are never read or changed.

## Test data

- **Fixtures** live in `tests/data` under the rules of [`tests/data/README.md`](../../tests/data/README.md): licence-clean, no personal data, small, listed in a manifest with checksums. A test fails if the folder and the manifest disagree.
- **Synthetic sky images** (`cloudscope/sim/synthetic_sky.hpp`) come with ground truth: cloud mask and fraction, Sun centre and radius, exact number of saturated pixels. Use them whenever a test needs to know the right answer, and for benchmarks at any resolution.
- Real camera recordings are never committed.

## Coverage

Requirement NFR-MNT-03: at least 70 % of the lines of the core library are executed by the tests. CI measures this on every push (job *Coverage*: GCC, gcov, gcovr), fails below the threshold, and uploads the line-by-line HTML report as the artifact `cloudscope-coverage`. `python tools/ci/status.py` shows the figure.

Locally (Linux or the Docker image, with `gcovr` installed):

```
cmake --preset linux-x64 -B build/coverage-build -DCLOUDSCOPE_COVERAGE=ON
cmake --build build/coverage-build --config Debug
ctest --test-dir build/coverage-build -C Debug
python3 tools/ci/coverage_report.py --build build/coverage-build --output build/coverage
```

Coverage says which lines ran, not whether the tests check anything. A new module is reviewed for *what its tests assert*; the percentage is only a guard against untested files.

## What CI runs

All tiers, Debug and Release, on Windows, Debian 13 (x64 and arm64) and Ubuntu 26.04, plus coverage and the Python tool tests: see [`ci.md`](ci.md).
