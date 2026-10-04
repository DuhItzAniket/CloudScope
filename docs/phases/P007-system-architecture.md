# P007 — System architecture

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Describe CloudScope's structure (C4 context, containers, components), its runtime behaviour (threads, data flow, failure handling) and the conventions shared with STRATIA, so that implementation phases have a fixed frame.

## Requirements covered
Architectural support for all SRS groups; explicitly FR-CTL-01/02 (HAL, simulators), FR-REM-07 (remote parity), FR-SAF-01…05 (two-layer safety), NFR-DATA-01 (time), NFR-PERF-01 (frame pipeline).

## Design notes
- **Containers:** `libcloudscope-core` (all domain logic, no UI or server code), `cloudscoped` (daemon), CloudScope Desktop, web dashboard, firmware, catalogue (SQLite + files), Python tools.
- **One Session API, two transports:** UIs only use `ISession`; `LocalSession` runs the core in-process, `RemoteSession` talks to the daemon, which wraps a `LocalSession` (detailed in ADR-003).
- **Thread model** with explicit "never does" rules (e.g. the safety supervisor never waits on another thread; inference never queues frames; the recorder never back-pressures acquisition).
- **Safety sequence:** host supervisor checks every path, firmware enforces limits and an expiring keep-out zone; heartbeat loss → stop → park.
- **Conventions:** UTC + monotonic time, ENU / azimuth-from-north / elevation-from-horizon, image and camera frames, SI units; identical to the interim logger and to STRATIA's contract (ADR-012).

## Work log
1. Wrote `docs/arch/architecture.md` with six Mermaid diagrams (context, containers, session API, components, capture sequence, motion-with-safety sequence) and tables for responsibilities, threads, failures, conventions and repository mapping.
2. Removed semicolons from sequence-diagram messages (Mermaid treats `;` as a statement separator) and corrected one wording error ("shared memory" → "process memory" for local mode).

## Verification
- All six diagrams were rendered with Mermaid 11 (the library GitHub uses) in a browser: `0..5: OK` (SVG sizes 15–154 kB), no parse errors.
- Every container and component maps to a repository folder (section 6) and to SRS requirements (component table).

## Exit criteria
- [x] C4 levels 1–3 documented.
- [x] Threading model, data flows and failure handling documented.
- [x] Reviewed against the SRS (component ↔ requirement table).

## Safety & failure-mode notes
Failure table (section 4.4) is the starting point for the fault-injection suite (P063, P081).

## Deviations & next phase
None. Next: **P008 — Architecture decision records**.
