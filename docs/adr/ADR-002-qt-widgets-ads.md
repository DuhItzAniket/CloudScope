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
