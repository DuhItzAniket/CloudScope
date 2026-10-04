# Building CloudScope

CloudScope's host software (core library, tools, later the daemon and the desktop application) is C++20 with Qt 6.8 or newer, built with CMake presets and Ninja.

| Platform | Preset | Compiler | Where the libraries come from |
|---|---|---|---|
| Windows 10/11 x64 | `windows-msvc` | MSVC 2022 (17.8 or newer) | [vcpkg](https://vcpkg.io) manifest `vcpkg.json`; Qt from the Qt installer or aqtinstall |
| Linux x86-64: Debian 13 or Ubuntu 26.04 | `linux-x64` | GCC 14 / GCC 15 | Distribution packages |
| Raspberry Pi 5, Raspberry Pi OS (Debian 13 "Trixie", 64-bit) | `linux-aarch64` | GCC 14 | Debian packages |

Debian 12 and Ubuntu 24.04 are **not** supported: they ship Qt 6.4. Other distributions work if they provide Qt ≥ 6.8 and the library versions below; they are not tested.

## Windows

Install once:

1. **Visual Studio 2022 Build Tools** (or Visual Studio 2022) with the workload *Desktop development with C++* and the component *vcpkg package manager*.
2. **CMake** ≥ 3.25 and **Ninja**.
3. **Qt 6.8 or newer** for MSVC 2022 64-bit, for example in `C:\Qt\6.9.3\msvc2022_64`.

Then, in a normal command prompt at the repository root:

```bat
tools\build\build.bat
```

This configures, builds the Debug configuration and runs the tests. The first run compiles the libraries listed in `vcpkg.json` (about 10 minutes, mostly OpenCV); later runs reuse vcpkg's binary cache.

- Another configuration: `tools\build\build.bat Release` (or `RelWithDebInfo`).
- Qt somewhere else: `set QT_ROOT_DIR=D:\Qt\6.8.3\msvc2022_64` before calling the script.
- Your own vcpkg clone instead of the one shipped with Visual Studio: `set VCPKG_ROOT=C:\path\to\vcpkg`.

To work step by step, prepare the shell once with `tools\build\win-env.bat`, then use the presets directly:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config Release
ctest --preset windows-msvc -C Release
```

Executables and all DLLs they need are in `build\windows-msvc\bin\<Config>\`, so they start from there without changing `PATH`.

## Debian 13, Raspberry Pi OS and Ubuntu 26.04

```sh
sh tools/build/install-deps-debian.sh     # once; uses sudo
sh tools/build/build.sh                   # configure, build Debug, run tests
sh tools/build/build.sh Release
```

`build.sh` picks `linux-x64` or `linux-aarch64` from the machine type. A preset refuses to configure on the wrong architecture.

`install-deps-debian.sh` is the single list of Linux build dependencies; the Docker image and CI use the same script.

## Linux build on a Windows machine (Docker)

This is how the Linux and arm64 builds are checked before pushing:

```sh
docker build -f tools/build/Dockerfile.linux -t cloudscope-dev:trixie tools/build
docker run --rm -v "<repository path>:/src" cloudscope-dev:trixie sh tools/build/build.sh
```

For arm64 (emulated, slow but faithful), add `--platform linux/arm64` to both commands and use another image tag. For Ubuntu 26.04 (Qt 6.10, GCC 15), add `--build-arg BASE=ubuntu:26.04` to the build command.

## Build options

| CMake option | Default | Meaning |
|---|---|---|
| `CLOUDSCOPE_BUILD_TESTS` | `ON` | Build the unit tests (needs Catch2) |
| `CLOUDSCOPE_WARNINGS_AS_ERRORS` | `OFF` | Fail the build on any compiler warning in CloudScope code; CI turns this on |

Pass options at configure time, for example `cmake --preset linux-x64 -DCLOUDSCOPE_WARNINGS_AS_ERRORS=ON`.

## Library versions

Code must compile against the **minimum** column: those are the versions in Debian 13, which Raspberry Pi OS uses.

| Library | Minimum (Debian 13) | Windows (vcpkg baseline 2026.07.29) | Used for |
|---|---|---|---|
| Qt (Core) | 6.8.2 | 6.8 or newer from the Qt installer | Application framework |
| OpenCV | 4.10.0 | 4.12.0 | Image processing and codecs |
| spdlog / fmt | 1.15.2 / 10.1 | 1.17.0 / 12.2.0 | Logging and formatting |
| toml++ | 3.4.0 | 3.4.0 | Configuration files |
| nlohmann-json | 3.11.3 | 3.12.0 | JSON sidecars and API payloads |
| CFITSIO | 4.6.2 | 4.6.4 | FITS files |
| libjpeg-turbo | 2.1.5 | 3.2.0 | JPEG and MJPEG (use the 2.1 TurboJPEG API: `tjCompress2`, `tjDecompress2`) |
| Catch2 | 3.7.1 | 3.15.3 | Unit tests |

The vcpkg versions are pinned by `builtin-baseline` in `vcpkg.json`. To update them, change the baseline to a newer vcpkg release commit, rebuild, run the tests and update this table.

## Checking a build

```
cloudscope-info --self-test
```

prints the version, git revision, compiler and the version of every library, then makes each library do real work (16-bit PNG/TIFF and FITS round trips, a JPEG round trip, TOML and JSON parsing, logging). The exit code is 1 if a check fails. Add `--json` for machine-readable output.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` | `VCPKG_ROOT` is empty: run `tools\build\win-env.bat` first, or install the vcpkg component of Visual Studio |
| `Could not find a package configuration file provided by "Qt6"` (Windows) | Set `QT_ROOT_DIR` to the Qt kit folder that contains `bin\Qt6Core.dll` |
| `This preset is for aarch64, but this machine builds for x86_64` | Use the preset that matches the machine, or `tools/build/build.sh` |
| Git revision shows `unknown` | The source folder is not a git checkout, or `git` is not installed |
