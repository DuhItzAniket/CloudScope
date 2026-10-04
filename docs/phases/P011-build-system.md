# P011 — Build system

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
One build system for Windows x64, Linux x64 and Raspberry Pi (arm64): CMake presets, pinned dependencies, and a first piece of `libcloudscope-core` that proves every library links and works on each platform.

## Requirements covered
NFR-MNT-01 (one documented build command per platform), FR-PLT-01 (platforms; amended, see Deviations), NFR-MNT-05.

## Design notes
- **Presets** (`CMakePresets.json`): `windows-msvc`, `linux-x64`, `linux-aarch64`, all with the Ninja Multi-Config generator (Debug, RelWithDebInfo and Release from one configure). The two Linux presets are native builds; a preset refuses to configure on the wrong architecture (`cmake/CloudScopeArchitecture.cmake`).
- **Dependencies have two providers, one set of target names** (`cmake/CloudScopeDependencies.cmake`):
  - Windows: vcpkg manifest `vcpkg.json`, pinned to vcpkg release 2026.07.29 (`builtin-baseline`). OpenCV is built with only the features CloudScope needs (calib3d, jpeg, png, tiff, threads); Catch2 is a manifest feature installed only when tests are built.
  - Debian 13 / Raspberry Pi OS / Ubuntu 26.04: distribution packages, listed once in `tools/build/install-deps-debian.sh`. Building OpenCV from source on a Pi would take hours; the packaged versions are the ones users will run.
  - Minimum versions in `find_package` calls are Debian 13's, so the oldest supported library set is enforced at configure time.
- **Qt** ≥ 6.8 (ADR-001): found through `QT_ROOT_DIR` on Windows (Qt installer or aqtinstall), system packages on Linux. Qt keyword macros and implicit string conversions are disabled for all CloudScope targets.
- **Warnings**: `/W4` plus selected level-4 warnings on MSVC; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow …` on GCC. `CLOUDSCOPE_WARNINGS_AS_ERRORS=ON` turns them into errors (used for every verification build below, and by CI from P012).
- **First core module** (`core/…/common`): `build_info` (version, git revision refreshed at build time, compiler, platform, compiled and loaded version and licence of each library) and `self_test` (each library does real work: 16-bit PNG/TIFF and FITS round trips, JPEG round trip, TOML/JSON parsing, a log record through a sink, ISO 8601 time). Exposed by the `cloudscope-info` executable; later used by bug reports, session records and the About dialog.
- Coding conventions: `docs/dev/cpp_style.md`. Build instructions: `docs/dev/building.md`.

## Work log
1. Checked the toolchain on the development laptop: CMake 4.4.3, Ninja 1.13.2, MSVC 19.44 (Build Tools 2022 17.14, with its bundled vcpkg and clang tools), Qt 6.9.3 (msvc2022_64), Docker Desktop 29.3.1 with WSL 2.
2. Wrote `vcpkg.json`; first dependency build 9 min (OpenCV 6.1 min), then served from vcpkg's binary cache (configure 29 s).
3. Wrote the CMake project: presets, architecture guard, compiler options, dependency wrappers (CFITSIO has a CMake package on vcpkg but only pkg-config on Debian), DLL copy helper for Windows, build-time git revision.
4. Wrote `build_info`, `self_test`, `cloudscope-info`, unit tests and command-line tests (Catch2; test executables share a `main()` with a `QCoreApplication`).
5. Wrote build scripts: `tools/build/win-env.bat` (locates MSVC, vcpkg, Qt), `build.bat`, `build.sh`, `install-deps-debian.sh`, `Dockerfile.linux`.
6. Built and tested in four environments (table below), including arm64 under emulation.
7. Checked Ubuntu: 24.04 ships Qt 6.4.2 (too old), 26.04 ships Qt 6.10.2 and builds cleanly. Amended FR-PLT-01 (SRS v1.4) and the plan row for P012.
8. Fixed `.gitignore`: the pattern `build/` also ignored `tools/build/`; build-output patterns are now anchored to the repository root.

## Verification
All builds with `CLOUDSCOPE_WARNINGS_AS_ERRORS=ON`; no warnings.

| Environment | Compiler | Qt | OpenCV | CFITSIO | libjpeg-turbo | Tests (Debug / Release) |
|---|---|---|---|---|---|---|
| Windows 11 x64 | MSVC 19.44 | 6.9.3 | 4.12.0 | 4.6.4 | 3.2.0 | 15/15 · 15/15 |
| Debian 13 x64 (Docker) | GCC 14.2 | 6.8.2 | 4.10.0 | 4.6.2 | 2.1.5 | 14/14 · 14/14 |
| Debian 13 arm64 (Docker, QEMU emulation) | GCC 14.2 | 6.8.2 | 4.10.0 | 4.6.2 | 2.1.5 | 14/14 · 14/14 |
| Ubuntu 26.04 x64 (Docker) | GCC 15.2 | 6.10.2 | 4.10.0 | 4.6.3 | 2.1.5 | 14/14 · 14/14 |

Windows has one more test than Linux: the no-console regression test below.

`cloudscope-info --self-test` on Windows (Release):

```
CloudScope 0.2.0 (git b59a591cb9-dirty)
Build:    Release, MSVC 19.44.35229.0, Windows x86_64
Libraries:
  Qt             6.9.3      (loaded: 6.9.3)  [LGPL-3.0-only]
  OpenCV         4.12.0     (loaded: 4.12.0)  [Apache-2.0]
  spdlog         1.17.0      [MIT]
  fmt            12.2.0      [MIT]
  toml++         3.4.0       [MIT]
  nlohmann-json  3.12.0      [MIT]
  CFITSIO        4.6.4      (loaded: 4.6.4)  [CFITSIO]
  libjpeg-turbo  3.2.0       [IJG AND BSD-3-Clause AND Zlib]
Self-test:
  PASS  qt-core: UTC timestamp with milliseconds parsed and formatted
  PASS  opencv: 16-bit PNG and TIFF round trips are lossless
  PASS  libjpeg-turbo: JPEG round trip, largest error 3 of 255 grey levels
  PASS  cfitsio: 16-bit FITS image and DATE-OBS keyword written and read back
  PASS  toml++: TOML document parsed
  PASS  nlohmann-json: JSON document parsed and serialised
  PASS  spdlog-fmt: log record formatted and delivered to a sink
```

The same report on the emulated Raspberry Pi-like system reads `Build: Debug, GNU 14.2.0, Linux aarch64` with Qt 6.8.2, and all seven checks pass.

Other checks:
- Architecture guard: `cmake --preset linux-aarch64` on an x86-64 container stops with "This preset is for aarch64, but this machine builds for x86_64".
- Clean checkout: a fresh `git clone` of this commit built and passed all tests with the single documented command on Windows (`tools\build\build.bat`) and in the Debian 13 container (`sh tools/build/build.sh`).

## Exit criteria
- [x] Hello-core builds on Windows + Linux: `cloudscope-core`, `cloudscope-info` and the tests build and pass on Windows x64, Debian 13 x64, Debian 13 arm64 and Ubuntu 26.04 x64 (table above).

## Safety & failure-mode notes
- **Defect found and fixed: a command-line tool that waits forever.** `QCommandLineParser::process()` reports errors and `--help` in a *message box* on Windows when the program has no console and its output is not redirected. Started that way with a wrong option, `cloudscope-info` blocked until killed. For a daemon or a scheduled capture job this would be a silent hang. All CloudScope executables now use `parse()` and print errors themselves (rule in `docs/dev/cpp_style.md`); a regression test starts the program without a console and requires exit code 2. The test fails (20 s timeout) against the old code and passes against the fix.
- **Stale build folder trap:** configuring the `linux-aarch64` preset once on an x86-64 machine leaves cached compiler information behind; a later arm64 build in the same folder then believes it is x86-64. The architecture guard reports this instead of producing mislabelled binaries; the fix is to delete `build/linux-aarch64`.
- **Version drift between platforms** is deliberate and bounded: Windows uses newer library versions than Debian. Code must stay within the Debian 13 API level (for example the TurboJPEG 2.1 API). The Debian builds in CI (P012) are the guard.
- The test "loaded libraries match the headers they were compiled against" catches a DLL or shared object picked up from another installation at run time.

## Deviations & next phase
- **FR-PLT-01 amended (SRS v1.4):** "Ubuntu 24.04 x64" replaced by "Debian 13 and Ubuntu 26.04 LTS x64". Ubuntu 24.04 packages Qt 6.4.2, below the Qt 6.8 minimum of ADR-001. The P012 plan row now names the four CI environments.
- **vcpkg only on Windows.** The plan listed one vcpkg manifest; on Linux the distribution packages are used instead (reasons in Design notes). The manifest and the Debian package list name the same libraries.
- **`linux-aarch64` is a native preset**, not a cross-compilation: it runs on the Pi 5 itself or on an arm64 CI runner. Cross-compiling Qt applications needs a matching sysroot and brings no benefit at this code size.
- **Qt on the development laptop** comes from the Qt installer (6.9.3), not aqtinstall; aqtinstall installs Qt 6.8 on the Windows CI runner in P012, so the minimum Qt version is also exercised on Windows.
- The local vcpkg is the copy bundled with Visual Studio Build Tools; no separate clone was needed.
- Not yet done, by design: licence report for dependencies (ADR-010, P012); clang-format/clang-tidy configuration (P015). CFITSIO and libjpeg-turbo carry their own permissive licences (SPDX `CFITSIO`, `IJG`); the P012 licence check must list them as accepted.
- Next: **P012 — CI**.
