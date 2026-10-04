# C++ coding conventions

These rules apply to all C++ in `core/`, `daemon/`, `apps/` and `tests/`. Formatting and lint rules are enforced by tools from P015 (clang-format, clang-tidy); this document covers what tools cannot check.

## Language and libraries

- **C++20**, no compiler extensions. Code must build with MSVC 2022 and GCC 14, on x86-64 and arm64.
- **Qt ≥ 6.8** (ADR-001). No API newer than 6.8 and none deprecated before 6.8. Only LGPL Qt modules (ADR-010).
- Qt keyword macros are disabled (`QT_NO_KEYWORDS`): write `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`.
- No implicit `const char*` ↔ `QString` conversions: use `QStringLiteral`, `QLatin1StringView` or `QString::fromUtf8`.
- The core library uses the standard library and Qt Core for domain logic. Qt types appear in public core headers only where Qt is the point (signals, event loop integration); plain data crosses module boundaries as standard types.
- Use the 2.1 TurboJPEG API and other library features only up to the minimum versions in [`building.md`](building.md).

## Naming

| Element | Style | Example |
|---|---|---|
| Types, enumerators, concepts | `PascalCase` | `FrameMetadata`, `PixelFormat::Mono16` |
| Abstract interfaces | `I` + `PascalCase` | `IClock`, `ICamera` |
| Functions, variables, parameters | `snake_case` | `run_self_test()`, `frame_count` |
| Private data members | `snake_case_` with trailing underscore | `queue_`, `exposure_ms_` |
| Constants (`constexpr`, `const` at namespace scope) | `kPascalCase` | `kMaxFrameBytes` |
| Namespaces | lower case | `cloudscope`, `cloudscope::capture` |
| Macros (avoid) | `CLOUDSCOPE_UPPER_CASE` | `CLOUDSCOPE_VERSION` |
| Files | `snake_case.hpp` / `.cpp` | `build_info.hpp` |
| Overrides of Qt virtuals, Qt signals and slots | Qt's `camelCase` | `timerEvent`, `frameReady` |

Physical quantities carry their unit in the name unless the type already does: `exposure_ms`, `azimuth_deg`. Angles are degrees in files and APIs, radians only inside computations (ADR-012).

## Files and headers

- Public headers: `core/include/cloudscope/<component>/<name>.hpp`, included as `<cloudscope/component/name.hpp>`. Private headers stay next to their sources in `core/src/<component>/`.
- `#pragma once`. Include order: own header, CloudScope headers, third-party headers, standard library; each group sorted.
- A header starts with one or two comment lines saying what it is for.
- Everything is in namespace `cloudscope` (sub-namespace per component from P016 on). File-local helpers go in an unnamed namespace.

## Errors

- Expected failures (device unplugged, malformed file, timeout) are **return values**: `Expected<T>` with an `Error{code, message}` (`cloudscope/common/error.hpp`); return failures with `fail(ErrorCode::NotFound, "...")` and add where it happened with `with_context()`. Exceptions are for programming errors and for unrecoverable start-up failures only, and never cross thread or library boundaries.
- Error messages say what is wrong and, where possible, what to do, in words a user understands; they name the file, key or device concerned (NFR-USE-04).
- Functions that return a value the caller must look at are `[[nodiscard]]`.
- No silent fallbacks: if a requested setting cannot be applied, report it. Never substitute a default and present it as the requested or measured value (quality bar in `CONTRIBUTING.md`).

## Resources and threads

- RAII for every resource; no naked `new`/`delete`; C handles are wrapped in `std::unique_ptr` with a deleter.
- State which thread a class lives on in its header comment. Shared state is either immutable, owned by one thread and reached by message passing, or protected by a named mutex. The thread model is in `docs/arch/architecture.md` §4.1.
- No blocking I/O on the UI thread or the acquisition thread.

## Time, logging and configuration

- Code that needs the time takes an `IClock&` (`cloudscope/common/clock.hpp`); it never calls `std::chrono::system_clock::now()` itself. Tests use `ManualClock`. Intervals and schedules use monotonic time; stored times are `UtcTime`, written with `format_iso8601()`.
- Angles are `Degrees` or `Radians` (`units.hpp`), never bare `double`, in every interface.
- Log through `logger("component")` (`log.hpp`). Keep the reference when logging often: `static spdlog::logger& log = logger("camera");`. Do not log per frame above `debug` level. Never build log text that contains a credential on purpose; the redaction filter is a safety net, not a licence.
- Settings come from the configuration (`config.hpp`, `app_config.hpp`). A new setting needs: a key in `core/resources/config.schema.json` and `config.defaults.toml`, a row in `docs/manual/configuration.md`, and, if it changes the meaning of existing files, a migration and a new `schema_version`.

## Executables

- Exit codes: 0 success, 1 the requested operation failed, 2 wrong usage. Results go to standard output, diagnostics to standard error.
- Parse arguments with `QCommandLineParser::parse()` and report errors yourself. Never call `process()`, `showHelp()` or `showVersion()`: on Windows they open a message box when the program has no console (service, scheduled task, remote shell) and wait for a click forever. Found in P011; `tests/integration/test_cli_info.cpp` holds the regression test.
- No interactive prompts: every executable must be usable unattended.

## Comments and tests

- Comments explain why, constraints and units; they do not repeat the code.
- Test names are plain sentences without square brackets or semicolons: CMake treats those characters specially and test discovery then merges or loses tests (seen with CMake 3.31 in P013).
- Every public function has a unit test for its normal case and its failure cases. Tests use real libraries and synthetic data; simulated devices are labelled as simulated.
- Warnings are errors in CI (`CLOUDSCOPE_WARNINGS_AS_ERRORS`). Fix the cause; suppress a warning only with a comment that says why it is a false positive.
