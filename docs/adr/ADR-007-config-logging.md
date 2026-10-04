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

## Consequences
- The effective, merged configuration is stored in each session record for reproducibility.
- Unknown keys are errors, not silently ignored.
