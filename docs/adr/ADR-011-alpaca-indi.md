# ADR-011 — ASCOM Alpaca as client and server; INDI as an optional Linux backend

Status: Accepted     Date: 2026-10-04     Phase: P008     Evidence: `docs/research/P006_competitive_analysis_sources.md` §1.5, §1.8, §3.3

## Context
Astronomy software interoperates through two device standards:
- **ASCOM Alpaca:** HTTP/REST + JSON (`/api/v1/{device_type}/{n}/{method}`), cross-platform, UDP discovery on port 32227, ImageBytes for fast image transfer. Used as a client by SharpCap and N.I.N.A.; Windows COM applications reach Alpaca devices through the ASCOM Platform's Dynamic Clients. The specification states it purposely has no hardened security and is meant for an isolated local network.
- **INDI:** XML over TCP (default port 7624), client/server with drivers as separate processes; client library LGPL-2.1; strongest on Linux and the Raspberry Pi (KStars/Ekos, indi-allsky). No authentication mechanism was found in its protocol documentation.

CloudScope wants to (a) drive third-party mounts and sensors without writing a driver for each, and (b) let other software use CloudScope's sky assessment (cloud cover, safety) — a natural way to put STRATIA's output into existing observatory workflows.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| Own HAL drivers only | Full control | Every third-party device needs new code; no interop with existing software |
| ASCOM COM | Huge Windows ecosystem | Windows-only; not usable on the Pi |
| **Alpaca client + server** | Cross-platform REST; reaches SharpCap, N.I.N.A. and (through Dynamic Clients) all COM apps | No built-in security; must stay on the LAN |
| **INDI client** | Reuses many Linux drivers (libcamera, ZWO, AAG CloudWatcher, GPIO) | Linux-first; separate XML protocol; no auth |
| INDI server (CloudScope as INDI device) | Ekos users could consume CloudScope | Extra protocol to maintain; Alpaca already covers the main clients |

## Decision
1. **Alpaca client drivers** in the HAL for Telescope (alt-az slews used as a pan-tilt), Focuser, Switch and ObservingConditions, with discovery (FR-CTL-12, P055). They are ordinary HAL drivers, so safety (keep-out zone, limits) still applies on the host.
2. **Alpaca server** in the daemon exposing **SafetyMonitor** (`issafe` from CloudScope's configurable sky-safety policy) and **ObservingConditions** (`cloudcover` and, when sensors exist, `skybrightness`, `skytemperature`, `temperature`, `humidity`). Quantities without a standard property (cloud genus, cloud-base height and its interval) are published through Alpaca `action` / `supportedactions` with documented action names (FR-REM-10, P084).
3. The Alpaca server is **off by default**, binds only to an explicitly chosen LAN interface, and is never exposed to the internet; remote access uses CloudScope's authenticated API (FR-SEC-*).
4. **INDI client** backend on Linux, optional (FR-CTL-13, P055), using the LGPL-2.1 client library dynamically linked. INDIGO devices are reached through INDIGO's Alpaca bridge rather than a native client. No INDI server for now.
5. No ASCOM COM support: Windows COM applications reach CloudScope's Alpaca server through the ASCOM Platform's Dynamic Clients.

## Consequences
- The HAL capability model must express Alpaca and INDI devices alongside CSDP and Pi GPIO devices.
- An Alpaca conformance check (ConformU or an equivalent scripted test) is part of P055/P084 exit criteria.
- The sky-safety policy (thresholds on cloud cover, rain, uncertainty) becomes a configurable, documented component, because other observatories' roofs may close on it.
