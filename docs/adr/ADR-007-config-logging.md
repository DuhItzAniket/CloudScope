# ADR-007 — TOML configuration with schema validation; spdlog logging

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
Configuration must be human-readable, validated, layered (defaults → system → user → session) and migrated between versions (FR-PLT-03). Logs must be structured enough to diagnose field failures and must never contain secrets (FR-SEC-06).

## Options considered
| Option | Pros | Cons |
|---|---|---|
| **TOML (toml++, MIT) + JSON Schema validation** | Readable, comments allowed, typed values; schema gives precise error messages | Two technologies (TOML file, JSON Schema) |
| JSON | One format for files and API | No comments; noisy to hand-edit |
| YAML | Readable | Ambiguous typing ("NO" → false); large parsers |
| QSettings / INI | Built into Qt | Weak typing and nesting; no schema |

## Decision
- Configuration files in **TOML**, parsed with **toml++**, validated against a **JSON Schema**, with a `schema_version` key and migration functions.
- Logging with **spdlog** (MIT): rotating files, console, and an in-memory sink feeding the UI log console; Qt messages routed into spdlog. A redaction filter removes configured secret fields.
- Missions (ADR in P075) may use YAML or TOML; decided in that phase.

## Implementation notes (P013)
- **Schema validation is done by CloudScope's own validator** (`cloudscope/common/json_schema.hpp`) for a documented subset of JSON Schema 2020-12; a schema that uses a keyword outside the subset is rejected when it is loaded, so nothing is silently left unchecked. Reasons: Debian's `libvalijson-dev` depends on the Qt 5 and Poco development packages, `json-schema-validator` is not packaged in Debian, and a small validator can produce messages a user can act on ("unknown key (did you mean 'max_files'?)").
- **The rotating log file sink is CloudScope's own**, built on `std::filesystem` paths. spdlog's file sinks need a custom vcpkg triplet for wide-character file names on Windows, and log folders are under the user profile, whose name is often not ASCII.
- **Each configuration file states `schema_version`**; a file in an older format is migrated, backed up as `<name>.v<N>.bak` and rewritten. Comments are lost in the rewrite and remain in the backup.
- **Errors** are returned as `Expected<T>` (`tl::expected`, the C++20 stand-in for `std::expected`).
- User documentation: `docs/manual/configuration.md`.

## Consequences
- The effective, merged configuration is stored in each session record for reproducibility.
- Unknown keys are errors, not silently ignored.
