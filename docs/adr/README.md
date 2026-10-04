# Architecture decision records

Each ADR records one decision, the options considered and the consequences. Template: [`TEMPLATE.md`](TEMPLATE.md). Superseded ADRs stay in place with a pointer to their replacement.

| ADR | Decision | Status | Phase |
|---|---|---|---|
| [001](ADR-001-cpp-qt.md) | C++20 and Qt ≥ 6.8 for all host software; Raspberry Pi OS (Debian 13 "Trixie") as the Pi platform | Accepted | P008 |
| [002](ADR-002-qt-widgets-ads.md) | Qt Widgets + Qt Advanced Docking System for the desktop UI | Accepted | P008 |
| [003](ADR-003-session-api-daemon.md) | Core library, daemon and clients behind one Session API | Accepted | P008 |
| [004](ADR-004-onnx-runtime.md) | ONNX Runtime for STRATIA inference | Accepted | P008 |
| [005](ADR-005-csdp-protocol.md) | CSDP: COBS + CRC-16 framed binary controller protocol, generated from one schema | Accepted | P008 |
| [006](ADR-006-remote-api-stack.md) | Drogon (MIT) for HTTP/WebSocket instead of GPL-only Qt HTTP Server; MJPEG then WebRTC | Accepted (spike in P084) | P008 |
| [007](ADR-007-config-logging.md) | TOML + JSON Schema configuration; spdlog logging | Accepted | P008 |
| [008](ADR-008-image-formats.md) | Image/video formats: JPEG, PNG/TIFF, FITS (cfitsio), SER (in-house), MP4 via external FFmpeg | Accepted | P008 |
| [009](ADR-009-firmware-toolchain.md) | PlatformIO, Arduino framework, FreeRTOS on ESP32 | Accepted | P008 |
| [010](ADR-010-licensing.md) | Apache-2.0; permissive or dynamically linked LGPL dependencies only; GPL-only modules excluded | Accepted | P008 |
| [011](ADR-011-alpaca-indi.md) | ASCOM Alpaca client and server; optional INDI client on Linux | Accepted | P008 |
| [012](ADR-012-time-coordinates.md) | Time and coordinate conventions shared with STRATIA | Accepted | P008 |
