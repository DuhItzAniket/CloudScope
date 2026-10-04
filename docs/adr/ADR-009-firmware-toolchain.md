# ADR-009 — PlatformIO firmware for ESP32 and Arduino Uno

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
Firmware must run on an ESP32 (full feature set: CSDP, PWM/PCA9685, IMU fusion, GPS/RTC/environment sensors, watchdog, OTA) and an Arduino Uno (CSDP-Lite), be easy for the owner to build and flash, and be testable in CI (FR-FW-*).

## Options considered
| Option | Pros | Cons |
|---|---|---|
| **PlatformIO with the Arduino framework (ESP32 core on FreeRTOS; AVR core for the Uno)** | One build tool for both boards; library manager; unit tests (native and on-target); CI-friendly CLI | Arduino abstractions hide some ESP-IDF features |
| ESP-IDF directly (ESP32) + Arduino IDE (Uno) | Full ESP32 control | Two unrelated toolchains |
| Arduino IDE only | Familiar | Poor CI support, weak project structure |

## Decision
Use **PlatformIO**, Arduino framework on both boards, with FreeRTOS tasks on the ESP32 (protocol, motion, sensors, safety at fixed priorities). Shared code (CSDP framing and generated messages) lives in `firmware/common/` and is unit-tested natively on the host in CI.

## Consequences
- The Arduino cores are LGPL-licensed and statically linked into firmware images; releases will include what LGPL requires for relinking (object files or build instructions), recorded in P098.
- ESP-IDF APIs may be called directly where the Arduino layer is insufficient (e.g. watchdog, OTA).
