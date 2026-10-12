# P039 — Capture panel

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Capture plans from the user interface: formats, the sequencer's kinds, progress with the time to the next picture,
sessions and the catalogue, driving P029 end to end (FR-SEQ-04).

## Requirements covered
FR-SEQ-04; the UI side of FR-SEQ-01/05/06, FR-REC-01/06/07, FR-DAT-01.

## Design notes
- `CapturePanel` turns its controls into a `CapturePlan` (single, burst, interval, bracket; count; interval;
  PNG / 16-bit TIFF / JPEG / FITS; JPEG quality; folder; file-name template; night profile switch) and validates
  it with the sequencer's `validate()` before starting.
- The sequencer runs on a worker thread of its own with the camera and hub of `CameraService`; recovery after a
  camera failure goes through `CameraService::recover_blocking()` (close, open, mode, restart), FR-SEQ-05.
- "Record into a catalogued session": a `Session` under the session root and a `Catalogue` opened on the worker
  thread (Qt SQL is thread-bound) receive every picture, as `cloudscope-camtool sequence --session` does; the
  session is closed with its counts at the end.
- Progress: the sequencer's statistics every 250 ms; for interval runs the countdown to the next slot.

## Work log
1. `capture_panel.cpp`; actions *Take one picture*, *Start capture*, *Stop capture*; the session browser reloads
   when a run finishes.

## Verification
- `ui.a burst from the panel writes pictures, sidecars, a session and catalogue rows`: without a camera the start
  is refused with a notice; with the simulated camera a burst of three JPEGs writes three pictures and sidecars
  into a new session folder, the session manifest, and three catalogue rows for that session with the camera's
  id; an interval run is stopped from the panel and reports `stopped_by_request`.

## Exit criteria
- [x] Drives P029 end-to-end (test above).

## Safety & failure-mode notes
- The disk guard of the plan (512 MiB by default) stops a run; the status bar shows the free space.
- A session that cannot be created falls back to the plain folder and says so; the pictures are never lost.

## Deviations & next phase
- None. Next: P040 — Session browser.
