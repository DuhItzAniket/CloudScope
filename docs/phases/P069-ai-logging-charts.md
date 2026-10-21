# P069 — AI logging & charts

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Log every result per frame to the catalogue and plot the timelines of cover, cloud-base height and genus, live
and replayed from the catalogue (FR-AI-07).

## Requirements covered
FR-AI-07.

## Design notes
- Catalogue (`core/session/catalogue`): a new table `ai_results` (session id, UTC, sequence, model, version,
  provider, oktas, cover, top genus with probability, étage probabilities, p(cloud overhead), cloud-base median
  and 80 % bounds, sky-parse fractions, reliability score, uncertain / night / geometry flags, run time) with
  indexes on time and (session, time); `add_ai_result()`, `ai_results(query)`, `ai_result_count()`. An existing
  catalogue gains the table when reopened (`CREATE TABLE IF NOT EXISTS`); the frame rows are untouched.
- `core/ai/result_log`: `ai_result_entry()` maps a `Prediction` to a row; `log_prediction()` writes it.
- Desktop: the main window writes every result into `<session_root>/catalogue.sqlite` with the capture panel's
  session id when a recording is running (empty otherwise), so a session's pictures and its AI results share the
  catalogue. `AiTimelinePanel`, dock "STRATIA timelines": the cover line, the cloud-base median with its 80 % band,
  and the top genus as a colour strip (uncertain results in the warning colour) over 10 minutes to 24 hours; a
  session chooser replays the logged rows of a session (`replay()`), "Live" returns to the running model.

## Work log
1. Catalogue table and API; result log; timeline model, chart widget and panel; main-window wiring (dock, logging,
   replay, session list refreshed after a capture).
2. Tests `tests/unit/test_ai_results.cpp` (a prediction becomes a row and reads back unchanged, counts per
   session, newest-first and time-window queries, an older catalogue gains the table) and `tests/ui/test_ai_timeline.cpp`
   (the model keeps points in time order inside its window; the chart paints; the panel takes live results, replays a
   session from the catalogue and ignores live results while replaying; the main window logs every result and shows
   it on the timeline).

## Verification
- `ctest -C Debug -R "unit\.a prediction becomes|ui\.(the timeline|the chart paints|the panel takes|the main window logs)"`:
  5 cases pass.
- Plots replay correctly: the replay test loads five logged rows of one session and finds the same five points,
  in order, with their cloud-base values; the Gate F runs (P072) leave results in the catalogue that the panel
  replays.

## Exit criteria
- [x] Charts match logged data (replay test; the chart draws from the same model the summary line counts).

## Safety & failure-mode notes
Logging failures are warnings in the log, never a stop: a catalogue that cannot be opened is reported once and
inference continues. Rows carry the model version and provider, so a later change of model is visible in the
data.

## Deviations & next phase
The sidecar JSON of recorded frames does not carry the AI result (it is written at capture time, before the
result exists); the catalogue row joins by session and time. Next: P070 — Inference benchmark tool.
