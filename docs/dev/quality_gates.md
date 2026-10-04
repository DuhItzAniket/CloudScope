# Quality gates

Every push to `master` must pass all of these; CI enforces them (`docs/dev/ci.md`). The policy for new code is **zero findings** (NFR-MNT-04): a finding is fixed, not postponed. Switching a check off needs a written reason next to the configuration.

| Gate | Tool | Configuration | Run it yourself |
|---|---|---|---|
| Compiler warnings are errors | MSVC `/W4 /WX`, GCC `-Wall -Wextra -Wpedantic -Wconversion … -Werror` | `cmake/CloudScopeCompilerOptions.cmake` | configure with `-DCLOUDSCOPE_WARNINGS_AS_ERRORS=ON` |
| Formatting | clang-format 19 | `.clang-format` | `python tools/dev/format.py` (add `--check` to only check) |
| Static analysis | clang-tidy 19 | `.clang-tidy`, `tests/.clang-tidy` | `python3 tools/dev/tidy.py --build build/linux-x64` (Linux or Docker image) |
| Memory and undefined-behaviour errors | AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer (GCC) | `CLOUDSCOPE_SANITIZE` | configure with `-DCLOUDSCOPE_SANITIZE=address,undefined`, build, run the tests |
| Tests | Catch2, CTest | `tests/` | `tools/build/build.bat`, `sh tools/build/build.sh` |
| Coverage ≥ 70 % of core-library lines | gcov, gcovr | `CLOUDSCOPE_COVERAGE` | `docs/dev/testing.md` |
| Third-party licences | `tools/ci/licence_check.py` | `packaging/licences/third_party.toml` | `docs/dev/ci.md` |
| No secrets in the repository, whole history | gitleaks | `.gitleaksignore` (reviewed false positives) | `gitleaks detect --source . --redact` |

## Formatting

`python tools/dev/format.py` rewrites every `.cpp`/`.hpp` under `core/`, `apps/` and `tests/`. It finds `clang-format` on `PATH` or in Visual Studio 2022 (which ships version 19, the same major version as CI). Style in one sentence: LLVM base, 4 spaces, 120 columns, opening brace on its own line for functions only.

## Static analysis

clang-tidy runs with these check families: `bugprone`, `cert`, `clang-analyzer`, `concurrency`, `cppcoreguidelines`, `misc`, `modernize`, `performance`, `portability`, `readability`, and enforces the naming rules of `docs/dev/cpp_style.md`. The checks that are off, each with its reason, are listed in `.clang-tidy`.

- Fix the code first. Use `// NOLINT(check-name): reason` only for a false positive, and always with the reason.
- clang-tidy needs Linux: use the Docker image (`docs/dev/building.md`) on Windows, with `clang-tidy` installed in it (`apt install clang-tidy`).
- A full run takes about ten minutes on the laptop; pass file names to check only what you changed.

## Sanitizers

The sanitizer build makes the tests fail on the first out-of-bounds access, use after free, memory leak, signed overflow, misaligned access or other undefined behaviour, including inside third-party code called by the tests. Code that passes its tests but does any of these is not done.

A thread-sanitizer build (`-DCLOUDSCOPE_SANITIZE=thread`) joins CI with the threading code in P016.

## Secrets

gitleaks scans every commit, not just the current files: a secret that was committed and later deleted is still exposed. If a real secret is ever found, removing it from the files is not enough: revoke it at its issuer first.

Made-up credentials in tests (the redaction tests need them) carry a `// gitleaks:allow` comment on the same line.

Not covered yet: known vulnerabilities in dependencies (NFR-SEC-02). vcpkg has no advisory database to check against; Windows dependencies follow the pinned vcpkg release and Linux ones receive the distribution's security updates. Revisited when remote access is built (P087).
