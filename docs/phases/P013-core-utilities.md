# P013 — Core utilities

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
The small building blocks every later module uses: error returns, unit-safe angles, clocks and timestamps, logging with redaction, and configuration files with schema validation and migration.

## Requirements covered
FR-PLT-03 (human-readable configuration, schema-validated, migrated automatically), NFR-DATA-01 (UTC timestamps with explicit offset, time source recorded: types and formatting; applied to frames in P022), FR-SEC-06 (secrets never written to logs: filter in place; reviewed again in P087), NFR-USE-04 (error messages say what happened and what to do: configuration errors), groundwork for FR-DSP-09 and FR-REM-03 (in-memory log buffer and listeners).

## Design notes
All in `core/include/cloudscope/common/` (ADR-007, ADR-012; implementation notes added to ADR-007).

| Header | What it provides |
|---|---|
| `error.hpp` | `Expected<T>` (`tl::expected`), `Error{code, message}` with 12 coarse `ErrorCode`s, `fail(...)`, `with_context(...)` |
| `units.hpp` | `Degrees`, `Radians` as distinct types (no implicit conversion from or to `double`), `wrap_360`, `wrap_180`, `angular_difference`, fractional `Milliseconds`/`Seconds` |
| `clock.hpp` | `UtcTime` (ms), `MonotonicTime`, `TimeSource` (host / ntp / gps-pps), `IClock` with `SystemClock` and `ManualClock`, `format_iso8601`, `parse_iso8601`, `format_file_stamp` |
| `log.hpp` | `logger("component")`, `init_logging`, rotating log file, in-memory buffer with listeners, redaction (`redact`, `register_log_secret`), Qt message routing |
| `json_schema.hpp` | Validator for a documented subset of JSON Schema 2020-12 with user-oriented messages; unsupported keywords are rejected when a schema is loaded |
| `config.hpp` | TOML ↔ JSON, atomic file writes, `ConfigFormat` (version, schema, defaults, migrations), layered `load_config` |
| `app_config.hpp` | CloudScope's `config.toml`: embedded schema and defaults (`core/resources/`), standard file locations, `[logging]` as `LoggingConfig` |

Decisions worth recording:
- **Timestamps** are written as `2026-10-04T12:34:56.789+00:00` and file stamps as `20261004T123456_789Z`: exactly the forms the interim sky logger already writes, so STRATIA reads both the same way. A timestamp without an offset is refused when parsing.
- **`SystemClock::time_source()`** reports `ntp` on Linux when the kernel says the clock is synchronised (`adjtimex`), otherwise `host`. On Windows it always reports `host` (no simple query exists); it never claims `gps-pps` by itself.
- **Each configuration file carries `schema_version`** and may be partial. Layers: defaults ← system file ← user file ← extra files ← overrides. Unknown keys and invalid values reject the file with its name and every problem; near-miss keys get a suggestion.
- **Migration**: an old file is migrated in memory, validated, then backed up (`<name>.v<N>.bak`, never overwriting a backup) and rewritten. If validation fails, the file is left untouched. Version 1 is the first format, so the application has no migrations yet; the machinery is tested with a three-version test format.
- **Own schema validator and own rotating file sink** instead of library ones: reasons in ADR-007.
- **Logging never recurses or deadlocks**: a message logged from inside an output or a listener is dropped; listeners run without any lock held. Every record is flushed to the file at once, because the lines just before a crash matter most.
- `cloudscope-info --show-config` and `--config FILE` print the configuration in effect and where it came from, or the error; this doubles as a checker for a user's file.
- New dependency: tl-expected (CC0-1.0, header-only), added to vcpkg, the Debian package list and the licence table.

## Work log
1. Checked library availability on Debian 13: `libexpected-dev` 1.1.0 exists; `libvalijson-dev` exists but depends on Qt 5 and Poco development packages; spdlog's wide-character file names need a custom vcpkg triplet.
2. Wrote the seven modules and their tests; added the two embedded resources through Qt's resource system.
3. Extended `cloudscope-info` and its command-line tests (tests redirect the configuration folders into a temporary home, so a developer's own files are never read).
4. Built and tested on Windows, then Debian 13; fixed what the second compiler and the older CMake found (below).
5. Wrote `docs/manual/configuration.md`; extended `docs/dev/cpp_style.md` (interfaces, errors, time, logging, configuration, test names).

## Verification
Warnings as errors everywhere. Unit and command-line tests, Debug and Release:

| Environment | Tests |
|---|---|
| Windows 11 x64, MSVC 19.44, Qt 6.9.3 | 80/80 · 80/80 |
| Debian 13 x64 (Docker), GCC 14.2, Qt 6.8.2 | 79/79 · 79/79 |
| Ubuntu 26.04 x64 (Docker), GCC 15.2, Qt 6.10.2 | 79/79 · 79/79 |
| Debian 13 arm64 | by CI on the arm64 runner (result recorded in `PROJECT_STATE.md` after the push) |

(Windows has one extra, Windows-only test from P011.) From this phase on, arm64 is verified by CI on real arm64 hardware instead of local emulation: running the emulated build next to two other containers exhausted the laptop's 16 GB of memory and crashed Docker. 65 of the tests are new in this phase. What they establish, by module:

- **Errors, units:** value/error propagation; context prefixes; `Degrees`/`Radians`/`double` do not convert implicitly (checked at compile time); wrapping at the boundaries 0, ±180, 360 and for a tiny negative angle; shortest angular difference across north.
- **Clock:** formatting checked against Python for four instants including a leap day and a pre-1970 time; 10 accepted input forms (offsets such as +05:30 and +13:45, 0 to 9 fraction digits); 18 rejected forms each with its reason (no offset, 30 February, 29 February 2025, hour 24, leap second, …); round trip; `ManualClock` advance versus wall-clock step; system clock sanity.
- **Schema validator:** 20 single-violation cases with exact messages; unknown-key suggestions; all violations reported in order; partial-document mode; character (not byte) length; local references; a self-referencing schema terminates; 16 malformed or unsupported schemas rejected.
- **Configuration:** TOML ↔ JSON round trip keeping 64-bit integers; dates, `inf` and `null` refused with the key; atomic write leaves no temporary file and works in a folder with a non-ASCII name; format self-checks (broken migration chains); three-version migration; layer merge order; invalid file reports all problems with the file name; migrated file rewritten and original backed up byte for byte; existing backup not overwritten; file failing validation after migration left untouched; standard paths per platform; CloudScope's own format valid.
- **Logging:** 14 redaction cases (key/value, JSON, URL query, `Authorization`, bearer tokens, URL passwords, and three texts that must stay unchanged), idempotent; registered secrets; file lines match `time level thread [component] message` with a UTC time that parses; level changes reach existing loggers; rotation (3,600 records of 1 KB into 3 files of at most 1 MB, oldest deleted); append on restart; bad configuration refused and the previous outputs kept; memory buffer capacity; listener may log without recursion; 8 threads × 500 records, none lost, no torn lines; Qt messages routed with category and redaction.

`cloudscope-info --show-config` on the development laptop:

```
Configuration files, lowest priority first:
  system:  C:\ProgramData\CloudScope\config.toml  (not present)
  user:    C:\Users\Luikz\AppData\Roaming\CloudScope\config.toml  (not present)
Log folder: C:\Users\Luikz\AppData\Local\CloudScope\logs
Configuration in effect:
schema_version = 1

[logging]
console = true
directory = ''
file = true
level = 'info'
max_file_mb = 10
max_files = 5
```

Licence gate passes on Windows and Debian with the new component.

## Exit criteria
- [x] Unit tests: table above; CI result for this commit checked after the push.

## Safety & failure-mode notes
Problems found while building this phase, and what was done:
- **A recursive template exhausted the compiler's memory** (MSVC C1060) in the JSON-to-TOML conversion: a generic function called itself with a new lambda type at each level. Rewritten as two plain mutually recursive functions.
- **Test names with square brackets broke test discovery** on CMake 3.31 (Debian): from the first such name on, tests were merged into one entry and not run, while CMake 4 on Windows ran all of them. Test names are now plain sentences (rule added to the style guide). This is the kind of silent gap CI on the oldest toolchain exists to catch: locally on Windows everything looked complete.
- **Debian's tl-expected 1.1.0 identifies itself as 1.0.0** to CMake; the required version in `find_package` is lowered accordingly.
- `int64_t` is `long` on Linux and `long long` on Windows; mixed literals in one initializer list do not compile with GCC.

Residual risks:
- **Redaction is pattern-based.** It catches the usual shapes (key = value, `Authorization`, URL passwords) and registered values; a secret logged in an unusual shape is not caught. Code must not log credentials in the first place (style guide); FR-SEC-06 is re-inspected in P087.
- **Flushing every log record** costs one write per record; fine at normal rates, but per-frame logging above `debug` level is forbidden by the style guide for this reason.
- **Comments in a user's file are lost when it is migrated** (they stay in the backup). Stated in the manual.
- The standard-library regular expressions used for `pattern` and redaction are slow; both run on short strings at low rates. Not for per-frame use.

## Deviations & next phase
- ADR-007 said "validated against a JSON Schema" without naming a validator; an in-house subset validator is used (reasons in the ADR). `uniqueItems`, `$ref` and `const` are supported beyond what the configuration needs today, because sidecar and API schemas will need them.
- `SystemClock` cannot detect NTP synchronisation on Windows; it reports `host` there.
- Not in this phase: applying the logging configuration in a long-running application (the daemon and desktop application do that when they exist); `[site]`, `[storage]` and other sections arrive with their features.
- Next: **P014 — Test infrastructure**.
