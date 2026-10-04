# ADR-006 — Drogon for the daemon's HTTP/WebSocket server; MJPEG then WebRTC for video

Status: Accepted (verification spike in P084)     Date: 2026-10-04     Phase: P008

## Context
`cloudscoped` needs a REST API, WebSocket channels, TLS, and live video (FR-REM-02…05, FR-SEC-04). The plan originally named Qt HttpServer. Licence check: **Qt HTTP Server is available only under GPLv3 or a commercial licence** ([Qt docs](https://doc.qt.io/qt-6/qthttpserver-index.html)), which would make distributed daemon binaries GPLv3 and conflicts with the dependency policy (ADR-010). Qt WebSockets is LGPLv3/GPLv2 ([Qt docs](https://doc.qt.io/qt-6/qtwebsockets-index.html)).

## Options considered
| Option | Licence | Pros | Cons |
|---|---|---|---|
| Qt HttpServer + Qt WebSockets | GPLv3 / LGPLv3 | Same framework as the rest | GPL-only server module |
| **Drogon** | MIT | HTTP/1.1, HTTPS (OpenSSL), WebSocket server and client, Windows and Linux, ARM support ([repository](https://github.com/drogonframework/drogon)) | Its own event loop alongside Qt's |
| Boost.Beast | BSL-1.0 | Very flexible | Low-level: routing, sessions, TLS plumbing all hand-written |
| cpp-httplib | MIT | Header-only, simple | Thread-per-connection model; WebSocket support not confirmed |

## Decision
- HTTP, REST and WebSocket: **Drogon**, running its own event-loop threads inside the daemon and calling the thread-safe `LocalSession`.
- Video: **MJPEG over HTTP** first (simple, low latency on a LAN); **WebRTC via libdatachannel** (MPL-2.0) later for WAN use (FR-REM-05).
- TLS through OpenSSL; certificates managed by the daemon's admin commands.

## Consequences
- P084 starts with a spike: Drogon builds on Windows (MSVC), Linux x64 and Debian 13 arm64, with HTTPS and WebSocket working, before the API is built on it. If the spike fails, Boost.Beast is the fallback.
- No GPL-only Qt module enters the build.
