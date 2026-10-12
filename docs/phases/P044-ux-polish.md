# P044 — UX polish

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
High-DPI, scaling, tooltips, translation-ready strings and accessibility names across the workbench (NFR-USE-01
to NFR-USE-04).

## Requirements covered
NFR-USE-01, NFR-USE-02, NFR-USE-03, NFR-USE-04.

## Design notes
- High DPI: `Qt::HighDpiScaleFactorRoundingPolicy::PassThrough` before the application is created; every panel
  is laid out with layouts and sizes in layout units; cosmetic pens keep overlay lines one device pixel wide at
  any zoom.
- Every user-visible string goes through `tr()`; `QT_NO_CAST_FROM_ASCII` keeps raw literals out of `QString`.
- Every interactive control carries an accessible name (or its text), every action a status tip and a tooltip;
  the test of P045 walks the widget tree and fails on an unnamed control.
- Units: controls show the device's unit or say "not calibrated"; times are shown in UTC and local (configurable);
  sizes in MiB / GiB.
- Errors: `user_message()` adds what to do for device errors; configuration errors name the key.

## Work log
1. Accessible names, tooltips and status tips across all panels; the checklist in `docs/reviews/gate_D.md` §2.

## Verification
- `ui.every interactive control has an accessible name and every action a status tip`: no unnamed control in the
  main window, no action without a status tip, no shortcut conflict in the defaults.
- Manual on the laptop at 100 % and 150 % scaling: readable, no clipped labels, the live view and overlays scale.

## Exit criteria
- [x] UX checklist (`docs/reviews/gate_D.md` §2).

## Safety & failure-mode notes
- None beyond the checklist.

## Deviations & next phase
- Translations themselves (`.ts` files) are not produced; the strings are ready for `lupdate`. Next: P045 — UI
  test automation.
