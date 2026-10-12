# P033 — App shell

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
The desktop application's frame: a main window of dockable panels whose arrangement and named layouts survive a
restart, menus and shortcuts for every action, and the dark and red night-vision themes (FR-DSP-08).

## Requirements covered
FR-DSP-08 (dockable panels, persistent layout, dark and night themes); groundwork for FR-DSP-01/09/10 (the panels
of the later phases hang in this shell).

## Design notes
- ADR-002: Qt Widgets with the **Qt Advanced Docking System** (ADS). Debian 13 packages ADS for Qt 5 only, so it
  is built from source at configure time (`FetchContent`, release 4.5.0, shared library, LGPL-2.1) on every
  platform; the ADR carries the implementation note. `cmake/CloudScopeDependencies.cmake` adds Qt Gui, Widgets,
  OpenGL and OpenGLWidgets, SDL2 (for P043) and ADS; `tools/build/install-deps-debian.sh` adds
  `qt6-base-private-dev`, `libxcb1-dev`, `libgl-dev`, `libsdl2-dev` and `fonts-dejavu-core`.
- `apps/desktop/`: a static library (`cloudscope_desktop_lib`, everything but `main()`) and the executable
  `cloudscope-desktop`, so that the user-interface tests link the real widgets.
- `AppContext` (`app_context.hpp`): configuration, paths, clock, device registry and event bus for the whole
  application, plus the typed `[desktop]` section of `config.toml` (theme, renderer, session root, default camera,
  time display, units, calibration file, site, pointing, shortcuts). `update_user_config()` merges a partial
  document into the user's `config.toml` after validating the whole configuration in place of the user layer, so
  an invalid value is refused with the validator's message and nothing is written (used by P041 and the theme menu).
- `MainWindow`: the live view is the docking manager's central widget; camera and capture dock on the left,
  histogram and image tools on the right, sessions and console at the bottom. Layout, window geometry and the
  named perspectives are saved to `desktop-state.ini` next to `config.toml` on close and restored at start;
  `--reset-layout` skips the restore. `ShortcutRegistry` (P043) owns the actions.
- Themes (`theme.hpp`): Fusion style with a palette and a short stylesheet. The night theme is red only; the preview
  pipeline renders the live picture in red under it (P036 `night_vision()`), recordings are untouched.
- Windows runtime: `cloudscope_copy_runtime_dlls(<target> GUI)` copies the Qt platform plugins (windows, offscreen)
  and the Windows style next to the executable, where Qt looks for them once `Qt6Core.dll` lives there.

## Work log
1. Build system: Qt GUI modules, SDL2 via vcpkg and Debian, ADS from source with its headers marked as system
   headers and its library placed with CloudScope's binaries; licence table entries (Qt modules, ADS, SDL2, libxcb).
2. Configuration: `[desktop]` section in `config.schema.json` and `config.defaults.toml`; rows in
   `docs/manual/configuration.md`.
3. `apps/desktop`: `AppContext`, `Theme`, `ShortcutRegistry`, `MainWindow` with docks, menus, perspectives and the
   state file; `main.cpp` with `--config`, `--camera`, `--mode`, `--start`, `--renderer`, `--theme`,
   `--reset-layout`, `--exit-after`, `--no-gamepad` (parsed with `parse()`, never `process()`).
4. Tests: `tests/ui/` executable with a `QApplication` main on the offscreen platform (`cloudscope_add_tests(... GUI)`
   sets `QT_QPA_PLATFORM=offscreen` for CTest); `DesktopFixture` redirects every folder into a temporary workspace.
5. Manual: `docs/manual/desktop.md`.

## Verification
- `ui.the panel layout and the saved perspectives persist across restarts`: two panels closed and a perspective
  "night ops" saved in one window; a second window restores the closed panels and lists the perspective; a window
  started with `restore_layout = false` shows the default layout.
- `ui.switching the theme changes the palette, the live view and the configuration file`: the application palette,
  the preview's night rendering and `theme = "night"` in the user's `config.toml`.
- `ui.the window starts honest`: camera, mount and AI segments say none / not available; the disk is known.
- Manual run on the laptop: `cloudscope-desktop` starts, panels drag, tab, float and come back after a restart;
  `cloudscope-desktop --reset-layout` gives the default arrangement. Suite: 45 of 45 ui tests pass in Debug and Release on Windows; the whole suite passes (330 tests in Release, 329 in Debug).

## Exit criteria
- [x] Layouts persist across restarts (test above and the manual restart).

## Safety & failure-mode notes
- A state file from another layout version is ignored (`docks/version`), never half-applied.
- Writing the theme choice goes through the validated configuration update: a broken user file is reported, not
  overwritten.

## Deviations & next phase
- ADS is fetched from source rather than taken from a package (Debian ships a Qt 5 build); the first configure
  needs network access once.
- Next: P034 — Live view.
