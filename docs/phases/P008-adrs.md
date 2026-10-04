# P008 — Architecture decision records

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Record the major technical decisions with their alternatives and consequences, verifying the facts they depend on (licences, platform versions, binary availability) instead of assuming them.

## Requirements covered
Constrains every later phase; amends FR-PLT-01 (SRS v1.2).

## Design notes
Twelve ADRs (index in `docs/adr/README.md`): C++20/Qt ≥ 6.8, Qt Widgets + ADS, one Session API, ONNX Runtime, CSDP protocol, Drogon for HTTP/WebSocket, TOML + spdlog, image formats, PlatformIO firmware, licensing policy, Alpaca/INDI interoperability, shared time/coordinate conventions.

## Work log
1. Verified the facts behind the decisions from primary sources (see Verification).
2. Wrote ADR-001…012 and the ADR index.
3. **Two plan decisions changed because of verified facts:**
   - The plan named Qt HttpServer for the daemon. Qt's own documentation states Qt HTTP Server is available **only under GPLv3 or a commercial licence**, which would conflict with the Apache-2.0 licence for distributed binaries → **Drogon (MIT)** chosen (ADR-006), with a build spike in P084.
   - The SRS named Raspberry Pi OS **Bookworm** (Qt 6.4.2). Raspberry Pi OS is now based on **Debian 13 "Trixie"**, which ships **Qt 6.8.2** for arm64 → minimum Qt 6.8 and Trixie as the Pi platform (ADR-001, SRS v1.2 FR-PLT-01).
4. Charts cannot use Qt Graphs (GPLv3-only per Qt docs) → QPainter or a permissive library, decided in P069 (ADR-010).
5. Updated `docs/PLAN.md` (ADR table rows 001 and 006), the SRS constraint C2, and the sky logger README (runs on Bookworm or Trixie).

## Verification
| Fact | Source | Result |
|---|---|---|
| Qt HTTP Server licence | doc.qt.io/qt-6/qthttpserver-index.html | "commercial licenses … In addition … GNU General Public License, version 3" — GPL-only |
| Qt Graphs licence | doc.qt.io/qt-6/qtgraphs-index.html | GPLv3/commercial only |
| Qt WebSockets licence | doc.qt.io/qt-6/qtwebsockets-index.html | LGPLv3 or GPLv2 |
| Qt Serial Port licence | doc.qt.io/qt-6/qtserialport-index.html | LGPLv3 or GPLv2 |
| Debian 13 Qt version | packages.debian.org/trixie/qt6-base-dev | 6.8.2+dfsg-9+deb13u2, arm64 available |
| Raspberry Pi OS base | 9to5linux.com (Raspberry Pi OS now based on Debian 13) | Confirmed |
| Drogon | github.com/drogonframework/drogon | MIT; HTTPS (OpenSSL); WebSocket server and client; Windows and Linux; ARM support |
| Qt Advanced Docking System | github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | LGPL-2.1; Qt 5 and Qt 6; release 5.0 |
| ONNX Runtime binaries | GitHub releases API, `microsoft/onnxruntime` latest | v1.30.0 (2026-09-10) includes `onnxruntime-linux-aarch64-1.30.0.tgz`, Windows x64 CPU and CUDA 12/13 packages (a page summary had wrongly said no aarch64 package; the API listing is authoritative) |

## Exit criteria
- [x] ADR-001…010 accepted (plus ADR-011 interoperability and ADR-012 conventions).
- [x] Every ADR states context, options, decision and consequences.

## Safety & failure-mode notes
ADR-011 keeps the Alpaca server off by default and LAN-only because the Alpaca specification has no security by design.

## Deviations & next phase
- Two plan choices changed for verified licence/platform reasons (above).
- The Apache-2.0 licence remains the owner's choice to override (ADR-010).
- Next: **P009 — Hardware reference designs** (component research in progress).
