# ADR-010 — Apache-2.0 for CloudScope; dependency licence policy

Status: Accepted (licence confirmed by the owner, Gate R, 2026-10-04)     Date: 2026-10-04     Phase: P008

## Context
The repository needs a licence (Apache-2.0 was added as the default in P002), and every dependency must be compatible with distributing CloudScope binaries for Windows and Raspberry Pi.

## Decision
1. **CloudScope source code: Apache-2.0.** The owner may switch to MIT or another permissive licence at any time before v1.0; doing so requires updating `LICENSE` and this ADR.
2. **Allowed dependency licences:** permissive (MIT, BSD, Apache-2.0, BSL-1.0, zlib, ISC, MPL-2.0 file-level), and LGPL **only when dynamically linked** (host software). *Amended in P012:* the exact list is the `[policy]` section of `packaging/licences/third_party.toml`. It adds the permissive licences of libraries in use that have their own SPDX identifiers (`IJG` for libjpeg-turbo, `CFITSIO`, `libtiff`, `libpng-2.0`, `0BSD` for liblzma) and a separate class for the compiler and C runtime on Linux (glibc, LGPL; libstdc++/libgcc, GPL with the GCC runtime library exception), which are part of the operating system and are not shipped by CloudScope.
3. **Not allowed in host binaries:** GPL-only components. Verified examples from Qt's own documentation: **Qt HTTP Server** and **Qt Graphs** are GPLv3/commercial only ([HTTP Server](https://doc.qt.io/qt-6/qthttpserver-index.html), [Graphs](https://doc.qt.io/qt-6/qtgraphs-index.html)); also excluded: QCustomPlot (GPL). Charts and timelines (FR-AI-07, FR-CTL-11) are therefore drawn with `QPainter` or a permissively licensed plotting library chosen in P069.
4. **Qt modules in use** must be LGPL: Core, Gui, Widgets, Network, Serial Port (LGPLv3, [docs](https://doc.qt.io/qt-6/qtserialport-index.html)), WebSockets (LGPLv3, [docs](https://doc.qt.io/qt-6/qtwebsockets-index.html)), SQL. Each new Qt module is checked against its documentation page before use.
5. **Firmware:** Arduino cores (LGPL) are statically linked; release artefacts satisfy LGPL relinking terms (ADR-009).
6. **Data:** captured images are the owner's; STRATIA models carry their own licence in the model card (e.g. the DINOv3 licence requires attribution).

## Consequences
- P012 adds an automated licence report of all dependencies to CI; a disallowed licence fails the build. *Done in P012:* `tools/ci/licence_check.py` checks every CI build against the reviewed component table `packaging/licences/third_party.toml`; an unlisted library or DLL, a licence outside the policy, or an LGPL library that is not linked dynamically fails the job (`docs/dev/ci.md`).
- Packaging (P098) ships `THIRD_PARTY_NOTICES` with every dependency's licence.
