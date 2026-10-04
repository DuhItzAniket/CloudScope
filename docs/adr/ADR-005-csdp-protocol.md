# ADR-005 — CSDP: a small framed binary protocol for controllers

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
The host must talk to an ESP32 (USB serial or Wi-Fi), an Arduino Uno (USB serial, 2 KB RAM) and possibly future boards, discover their capabilities, stream IMU telemetry, send motion setpoints, and keep a safety heartbeat (FR-CTL-03/04, FR-FW-*, FR-SAF-02/04). Corrupted or partial messages must never move hardware.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| Firmata | Standard for host-controlled Arduinos | Pin-level only: no on-board fusion, no safety semantics, no capability model |
| MAVLink | Proven framing, heartbeats, has gimbal messages | Large message set; tight on an Uno's 2 KB; semantics built around drones |
| protobuf (nanopb) | Schema evolution | Varint decoding and code size on AVR; still needs framing |
| JSON lines | Human-readable | Parsing cost and RAM on an Uno; no integrity check |
| **Custom CSDP: COBS framing + CRC-16 + fixed little-endian message structs generated from one YAML schema** | Tiny on AVR; robust resynchronisation (COBS) and integrity (CRC); same code generated for host and firmware; capability handshake designed in | Our own protocol to document and test |

## Decision
Implement **CSDP** (CloudScope Device Protocol):
- Framing: COBS-encoded frames delimited by `0x00`, each carrying `[version, msg_id, seq, payload, CRC-16/CCITT]`.
- Messages defined once in `protocol/csdp.yaml`; a generator emits C headers for firmware and C++ for the host (FR-FW-05).
- Mandatory handshake (`HELLO` → `CAPABILITIES`), heartbeat in both directions, explicit `FAULT` and `ESTOP` messages, keep-out-zone and soft-limit messages that firmware enforces (FR-SAF-02/03).
- **CSDP-Lite** for the Uno: the same framing with a subset of messages (servos, raw IMU, heartbeat, e-stop).
- Design ideas borrowed from MAVLink: sequence numbers, heartbeats, versioned messages.

## Consequences
- The protocol specification (P047) includes framing test vectors; host and firmware run the same vectors in CI.
- Incompatible protocol versions are refused at handshake (FR-FW-03).
