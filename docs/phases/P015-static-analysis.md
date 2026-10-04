# P015 — Static analysis

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Let tools hold the line on code quality: one formatting, static analysis with every finding an error, sanitizers under the whole test suite, and a secret scan of the full history, all enforced in CI.

## Requirements covered
NFR-MNT-04 (formatting and static analysis with zero warnings), NFR-SEC-02 (secret scanning on every push; dependency vulnerability scanning deferred, see Deviations), risk K7 of Gate R (quality of AI-written code).

## Design notes
- **clang-format 19** (`.clang-format`): LLVM base, 4 spaces, 120 columns, function braces on their own line. `tools/dev/format.py` formats or checks; it finds the clang-format that ships with Visual Studio 2022 (19.1.5), and code formatted there passes the check with Debian's 19.1.7.
- **clang-tidy 19** (`.clang-tidy`, `tests/.clang-tidy`): families bugprone, cert, clang-analyzer, concurrency, cppcoreguidelines, misc, modernize, performance, portability, readability; naming rules of the style guide; `WarningsAsErrors: '*'`. 22 checks are off in product code, each with its reason in the file (style choices such as trailing return types, checks that contradict deliberate designs such as recursion over JSON trees, checks that cannot understand Qt/OpenCV umbrella headers); 9 more are off for tests because they fight the Catch2 macros. `tools/dev/tidy.py` runs it in parallel and lists each finding once.
- **Sanitizers**: build option `CLOUDSCOPE_SANITIZE` (GCC/Clang) with `-fno-sanitize-recover=all`, so the first finding fails the test that triggered it. CI job with address + undefined (which includes the leak detector).
- **Secret scan**: gitleaks over the whole history in CI (Debian's package, 8.16). Reviewed false positives are pinned in `.gitleaksignore`; made-up credentials in tests carry `gitleaks:allow`.
- All gates and how to run them: `docs/dev/quality_gates.md`.

## Work log
1. Wrote `.clang-format`; formatted the code base (17 files changed, about 115 lines).
2. Wrote the clang-tidy configuration and `tidy.py`; first run: **100 findings** in 23 files.
3. Of the 100, 78 were fixed in code. 22 went away by configuration: four checks switched off (19 findings: arithmetic parentheses, reference members in short-lived helpers, braced returns, and an analyser finding inside Catch2's own header) and one naming rule corrected to match the style guide (3). The second run still showed 9, some of them introduced by the fixes; those were fixed too. Changes worth naming:
   - `cloudscope-info`: nothing can leave `main()` as an exception any more (new exit code 3, "internal error"), and a failed write of the report makes the exit code non-zero.
   - Structs are initialised with designated initialisers throughout (26 sites): fields of the same type can no longer be swapped silently, e.g. the four strings of a library entry.
   - Range checks that must reject NaN go through one helper instead of `!(a && b)` expressions.
   - `ScopeExit` (new, `scope_exit.hpp`) replaces two hand-written local guard structs.
   - Enums have a one-byte underlying type; the build configuration header uses `constexpr` values instead of macros; classic algorithms became their ranges forms; the schema keyword check was split into small functions (cognitive complexity 53 → below 40).
4. Added the sanitizer option; added four tests for the two developer scripts; the first of them exposed a bug in `tidy.py` itself (Windows drive letters in paths were not parsed).
5. Ran gitleaks over the history: one finding, a made-up token in the log redaction test; marked and pinned.
6. Added three CI jobs: *Format and static analysis*, *Sanitizers*, *Secret scan*.

## Verification
| Check | Where | Result |
|---|---|---|
| Formatting | Debian container, clang-format 19.1.7 | 35 files, all formatted |
| clang-tidy | Debian container, clang-tidy 19.1.7 | first run 100 findings; second run 9; after the last fixes the five affected files report 0 (full run in CI) |
| Build with warnings as errors + tests | Windows (MSVC 19.44) | 96/96 |
| Build with warnings as errors + tests | Debian 13 (GCC 14.2) | 95/95 |
| AddressSanitizer + UndefinedBehaviorSanitizer + leak detector | Debian container, all tests | 95/95, no reports |
| gitleaks | Debian container, 59 commits | no leaks after the one reviewed false positive |
| Python tool tests | laptop | 72 (61 before; 4 new for `format.py`/`tidy.py`, 7 from P014) |

One new C++ test (`ScopeExit`); test counts are now 96 on Windows and 95 on Linux.

## Exit criteria
- [x] Zero-warnings policy on new code: compiler warnings, clang-format and clang-tidy are errors in CI; the code base starts at zero findings. CI result for this commit checked after the push.

## Safety & failure-mode notes
- **What the tools found that mattered.** No memory error, leak or undefined behaviour in 95 tests under the sanitizers. clang-tidy's substantive findings were: an exception could escape `main()`; the result of `fwrite` was ignored; an implicit widening multiplication; and the NaN-fragile range checks. Everything else was style and modernisation.
- **What the tools cannot find.** Wrong requirements, wrong physics, missing tests, race conditions that no test exercises. The thread sanitizer joins CI in P016 with the first multi-threaded code.
- **`NOLINT` is the escape hatch to watch.** One exists (`config.cpp`: a pointer passed together with its length, which the check cannot see). Each one must carry its reason; a growing number would mean the configuration is wrong.
- **Formatter versions.** clang-format output is only guaranteed stable within a major version. CI pins 19 through Debian 13; `format.py` warns when run with another major version.
- **Secret scanning after the fact is too late for a real secret**: it would already be public. The scan is a net, the rule remains "never commit a secret" (`CONTRIBUTING.md`).

## Deviations & next phase
- NFR-SEC-02 also asks for scanning dependencies for known vulnerabilities. Not done: vcpkg has no advisory database, and the Linux builds use distribution packages that receive security updates. Decision deferred to P087 (threat model for remote access), when network-facing dependencies (Drogon, TLS) arrive and the question becomes material.
- gitleaks comes from Debian 13 (8.16), not the latest upstream release (8.30): no binary download in CI, at the price of an older rule set.
- clang-tidy needs Linux here; on Windows it is run in the Docker image. A full run takes about 12 minutes on the laptop (six parallel jobs).
- Next: **P016 — Threading & event bus**.
