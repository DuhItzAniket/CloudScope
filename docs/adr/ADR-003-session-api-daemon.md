# ADR-003 — Core library, daemon and clients behind one Session API

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
CloudScope must run headless on a Raspberry Pi with remote access (FR-REM-01…08), and the desktop app must behave the same locally and remotely (FR-REM-07). A local live view of 16 MP frames must not pay network or encoding costs. Two processes must never open the same camera.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| Monolithic desktop app with an embedded server | Simple | Needs a display; Pi headless use awkward; UI and server failures coupled |
| Daemon always, desktop always a network client | One code path | Full-resolution live view over localhost is expensive; added latency for local use |
| **Core library + daemon + clients, all through one `ISession` interface with `LocalSession` and `RemoteSession` implementations** | Same UI code local or remote; local mode has zero-copy frames; daemon is a thin adapter | Two implementations of one interface must be kept in sync (mitigated by shared contract tests) |

## Decision
- `libcloudscope-core` contains all domain logic and no UI or server code.
- `ISession` (commands, queries, event stream, frame stream) is the only API that UIs use. `LocalSession` calls the core in-process; `RemoteSession` speaks REST/WebSocket to `cloudscoped`, which wraps a `LocalSession`.
- When a daemon is running on the same machine, the desktop app connects to it instead of opening devices itself.
- The same contract test suite runs against `LocalSession` and against `RemoteSession` + daemon.

## Consequences
- The OpenAPI document and the C++ interface are generated from, or checked against, one command list (P084).
- Authentication, roles and the control lock live in the daemon only (FR-SEC-*).
