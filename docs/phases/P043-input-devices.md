# P043 — Input devices

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Keyboard shortcuts for every frequent action, configurable and checked for conflicts; a gamepad (SDL2 game
controller) that drives the simulated mount (FR-DSP-10).

## Requirements covered
FR-DSP-10.

## Design notes
- `ShortcutRegistry` (`shortcuts.hpp`): actions with stable ids (`view.fit`), default keys and status tips;
  overrides from `desktop.shortcuts` in Qt's portable spelling; unknown ids and unparsable keys are refused by
  name; conflicts (two actions, one key) are listed and reported at start.
- `SdlGamepad` (`gamepad.hpp`): SDL2's game-controller API polled on a 50 ms timer, hot-plug followed.
  `MountJog`: left stick or D-pad → pan and tilt speed (dead zone, shaping, speed limited by the mount's
  capabilities), *A* stops, *B* is the emergency stop; the mount is the HAL interface, so the same code will drive
  a real mount in Stage E. The source is an interface so that the tests use a fake controller.
- SDL2 is a dependency on both platforms (vcpkg `sdl2`, Debian `libsdl2-dev`), zlib licence.

## Work log
1. `shortcuts.cpp`, `gamepad.cpp`; the preferences list and edit the shortcuts; the status bar shows the mount's
   position while a gamepad is connected.

## Verification
- `ui.shortcuts take their defaults, overrides from the configuration, and report conflicts`.
- `ui.the gamepad jog drives the simulated mount and stops it`: a fake controller at full right deflection moves
  the simulated mount's pan (the position advances over real time), stick up tilts up, release stops, *B* sets the
  emergency stop which refuses further moves until cleared.
- `ui.the SDL gamepad source starts without a controller and reports no input` (no hardware assumed).
- Manual: no gamepad on the development laptop; the SDL subsystem starts and the status bar says "no gamepad
  connected".

## Exit criteria
- [x] Gamepad drives simulated mount (test above).

## Safety & failure-mode notes
- The jog commands the HAL mount, which enforces its limits and the emergency stop; the host supervisor and the
  keep-out cone (P060) sit above it in Stage E.

## Deviations & next phase
- No physical gamepad was available: the SDL path is exercised without a controller; the jog logic is tested with
  the fake. Next: P044 — UX polish.
