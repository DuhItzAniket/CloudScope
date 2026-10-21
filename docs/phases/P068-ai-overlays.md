# P068 — AI overlays

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Show the model's results in the desktop application: masks and layers over the live view, genus with confidence,
cover and cloud-base height with its interval in a panel, and an honest "uncertain" state when the model or its
inputs say so (FR-AI-05, FR-AI-06).

## Requirements covered
FR-AI-05, FR-AI-06, FR-AI-02 (the provider in use is shown), NFR-PERF-05 (the UI never blocks on inference).

## Design notes
- `AiController` (desktop) owns the `InferenceService`, feeds it the newest preview frame with the geometry of
  P067 and marshals every result to the UI thread through `QtExecutor`; it loads the configured package at start
  (`[ai] model_dir`) and reports the engine in use.
- `AiPanel`, dock "STRATIA": model, provider, rate and timing; Load model… / Unload / Benchmark…; "Run on live
  frames"; overlay switches (masks, layers, badge, opacity, show masks of uncertain results); the latest result
  with the frame, the reliability line (reliable / UNCERTAIN with the reason), genus (top three with bars), the
  étages, oktas with the expected cover, and the cloud base with its 80 % and 95 % intervals and p(cloud overhead).
- `ai_overlay`: the mask picture (cloud white, Sun glare orange, invalid dark; layers low / mid / high in colour)
  drawn through the viewport over the frame, and the badge with the headline (`Cu 82 % · 3 okta · base 1.2 km`,
  or `AI: uncertain (…)`). FR-AI-06: an uncertain result hides the masks unless the operator asks for them, the
  badge always says so; `LiveViewScene` carries the prediction, the mask picture and the overlay settings.
- Main window: menu AI (Load model…, Models…, Run on live frames Ctrl+Shift+A, AI masks M), the dock next to the
  histogram, status bar `AI: <model> on <provider>, <ms>` (Active; Warning when uncertain; Idle when paused;
  Absent without a model), theme colours, and `ensure_projection()` so that the model receives the sky projection
  also when no sky overlay is switched on.

## Work log
1. Controller, panel, overlay painter, live-view hooks, main-window wiring, actions and menu.
2. Tests `tests/ui/test_ai.cpp`: the overlay picture tints cloud and glare, layers replace the plain tint, nothing
   is drawn for an uncertain result unless asked; the badge text; the live view paints the overlay over the picture
   and the badge in the corner; the panel shows a result with its uncertainty and reports the overlay and run
   switches; the main window loads the dummy model, runs it on a preview frame (signal spy), shows the result on
   the panel, the status bar and the live view, and pausing clears the live result.
3. The accessibility test of P044 (every control named) covers the new panel; the status bar keeps "not
   available" without a model, as the Stage D test expects.

## Verification
- `ctest -C Debug -R "ui\.(the overlay picture|the live view paints the overlay|the STRATIA panel|the main window
  runs)"`: 4 cases pass (the main-window case runs the 512 px dummy on the CPU in about 1.5 s).
- Gate F (P072): the real model on replayed B0268 frames and on the live camera through the same path.

## Exit criteria
- [x] Overlays only drawn when valid; "uncertain" state shown honestly (badge and panel; masks hidden by default
      for uncertain results; tests above).

## Safety & failure-mode notes
Results arrive on the UI thread by event; a result for an old frame is still shown (the badge names the frame
sequence in the panel) but the live view keeps the newest picture, so a slow model lags, never blocks. A model
that fails to load leaves the status bar on "not available" with the reason in the notifications.

## Deviations & next phase
None. Next: P069 — AI logging & charts.
