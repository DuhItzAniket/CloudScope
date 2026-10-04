# ADR-001 — C++20 and Qt 6.8+ for all host software

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
CloudScope needs a responsive, dense desktop workbench, a headless daemon, real-time frame handling (16 MP frames), and must run on Windows x64 and Raspberry Pi 5 (arm64). The prototype already used C++/Qt 6.9.3 with MSVC, OpenCV and ONNX Runtime, and that toolchain is installed on the development laptop. Raspberry Pi OS is now based on Debian 13 "Trixie", which packages **Qt 6.8.2** for arm64 ([Debian package](https://packages.debian.org/trixie/qt6-base-dev); [9to5Linux](https://9to5linux.com/raspberry-pi-os-is-now-based-on-debian-13-trixie-with-fresh-new-look)). The previous base (Bookworm) shipped Qt 6.4.2.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| **C++20 + Qt 6** | Native speed on the Pi; mature widgets, serial port, networking; existing toolchain and prototype code; LGPL for the modules we need | Slower to write than Python; build system complexity |
| Python + PySide6 | Fast development; same language as STRATIA | GIL and per-frame overhead at 16 MP; harder packaging for a polished app; still needs C++ for heavy paths |
| Rust + egui/Slint | Memory safety, good performance | No team experience; weaker camera/UVC ecosystem; immature docking UI |
| Electron/Tauri + Python backend | Web UI reuse | Heavy on the Pi; two runtimes; latency for live video |

## Decision
All host software (core library, daemon, desktop app) is written in **C++20** with **Qt 6, minimum version 6.8** (the version Raspberry Pi OS Trixie ships). Python stays for tools and STRATIA.

## Consequences
- Code must compile against Qt 6.8 even though the Windows machine has Qt 6.9.3; CI builds on Debian 13 (Qt 6.8.2) catch accidental use of newer APIs (P012).
- **SRS correction:** FR-PLT-01 named "Raspberry Pi OS Bookworm"; it is amended to Raspberry Pi OS based on Debian 13 (Trixie), 64-bit. Bookworm is not supported for the C++ applications (the Python sky logger still runs there).
- Only LGPL- or permissively-licensed Qt modules may be used (ADR-010).
