# ADR-002 — Qt Widgets with the Qt Advanced Docking System for the desktop UI

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
The goal is a SharpCap-class workbench: many simultaneous panels (live view, camera controls, histogram, capture, mount, telemetry, AI, log) that the user rearranges and that persist between sessions (FR-DSP-08). The prototype used QML.

## Options considered
| Option | Pros | Cons |
|---|---|---|
| **Qt Widgets + Qt Advanced Docking System (ADS)** | Proven dense desktop UIs; ADS provides tabbed, floating, nested docks with saved perspectives; LGPL-2.1, supports Qt 6, release 5.0 adds dark mode and Wayland ([repository](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)) | Less suited to touch screens |
| Qt Widgets + built-in `QDockWidget` | No extra dependency | Weak tabbing/nesting; awkward floating behaviour |
| QML (Qt Quick) | Smooth animation, touch-friendly | Docking frameworks immature; dense forms and tables are more work; the prototype's QML shell does not scale to dozens of panels |

## Decision
The desktop application uses **Qt Widgets** with the **Qt Advanced Docking System** (dynamically linked, LGPL-2.1). The live view renders through Qt's RHI/OpenGL path inside a widget. QML may be used later for a touch-first kiosk UI, which is not a v1.0 requirement.

## Consequences
- The legacy QML frontend is not carried forward; its layout ideas are reused.
- ADS is added as a dependency in P011 (built from source or vendored as a submodule, since vcpkg availability is not confirmed).
- Perspectives (saved layouts) satisfy FR-DSP-08.

## Implementation notes (P033)
- Debian 13 packages the Qt Advanced Docking System for Qt 5 only (`libqt-advanced-docking-system-dev` 3.8.3 depends on `qtbase5-dev`), so the library is **built from source at configure time** (CMake `FetchContent`, release tag 4.5.0, the version Ubuntu 26.04 ships) on every platform, as a shared library next to CloudScope's own binaries. One version everywhere; the Windows build does not use a vcpkg port for it.
- On Linux the library uses Qt's platform-native interface (`qt6-base-private-dev`) and `libxcb1-dev`; both are in `tools/build/install-deps-debian.sh`.
- The live view renders through `QOpenGLWidget` (Qt's OpenGL paint engine scales the picture on the GPU) with a software `QWidget` canvas as the fallback and for the offscreen tests; `QRhiWidget` was not used because the offscreen platform in CI has no graphics device.
- Gamepad input uses SDL2's game-controller API (zlib licence); Qt 6 has no gamepad module.
