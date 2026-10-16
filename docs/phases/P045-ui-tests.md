# P045 — UI test automation

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
The user-interface tests as part of the normal suite: Qt widgets driven in tests, a screenshot regression, all
running in CI on the offscreen platform.

## Requirements covered
NFR-MNT-02 (CI), NFR-MNT-03 (coverage of the desktop code through the `ui.` tier).

## Design notes
- `tests/ui/`: the executable `cloudscope-ui-tests` with a `QApplication` main
  (`tests/support/qt_widgets_catch_main.cpp`); `cloudscope_add_tests(... GUI)` copies the Qt platform plugins
  next to it on Windows and sets `QT_QPA_PLATFORM=offscreen` for CTest, so the tests need no display and no
  graphics driver (the live view uses its software canvas).
- `DesktopFixture` builds an `AppContext` whose configuration, state, profiles and sessions live in a temporary
  workspace with a non-ASCII name; USB cameras are off, the simulators on.
- Interaction: widgets are driven through their real signals and slots (`click()`, `activated()`,
  `editingFinished()`) and the camera service's worker is awaited with `wait_until()`.
- Screenshot regression: `paint_live_view()` renders a synthetic sky with the reticle and the grid into an image
  that is compared with the committed reference `tests/data/ui/live_view_reticle_grid.png` (mean absolute
  difference below 1 level, fewer than 0.5 % of pixels differing by more than 40); text is left out because fonts
  differ between platforms. `CLOUDSCOPE_UPDATE_REFERENCES=1` rewrites the reference.

## Work log
1. Test executable, fixture, 45 test cases across the Stage D phases (the whole tier runs in about 12 s, the
   100,000-row catalogue test being most of it); the reference picture and its manifest entry in
   `tests/data/manifest.json` (the fixture test accepts the project's own licence for generated fixtures).

## Verification
- Windows: 45 of 45 ui tests pass in Debug and Release on Windows; the whole suite passes (330 tests in Release, 329 in Debug).
- CI (Debian 13 x64 and arm64, Ubuntu 26.04, sanitizers): run #25 on `b3ee0e7`: all ten jobs green (Debian 13 x64 and arm64, Ubuntu 26.04 with Qt 6.10, Windows x64, address/undefined and thread sanitizers, coverage, format and static analysis, secret scan, Python tools) after two follow-up fixes (2c7cf8e, b3ee0e7).

## Exit criteria
- [x] Runs in CI (offscreen).

## Safety & failure-mode notes
- Tests that wait on the camera worker have generous time limits (5–30 s) so that a slow CI machine does not
  fail them; the CTest timeout for the tier is 120 s.

## Deviations & next phase
- None. Next: P046 — Gate D.
