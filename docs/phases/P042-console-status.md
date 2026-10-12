# P042 — Console & status

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
A searchable log console, notifications the operator must see, and a status bar for the camera, mount, sensors,
AI, remote clients and disk that is visible and accurate at all times (FR-DSP-09).

## Requirements covered
FR-DSP-09; NFR-USE-01 (absent subsystems say so).

## Design notes
- `LogModel` listens to the process log (`add_log_listener`, the in-memory sink of ADR-007) and seeds itself with
  the recent records; records are handed to the UI thread in batches and the model keeps the last 5,000.
  `ConsolePanel` filters by minimum level and by text in the message or the component, follows new messages, and
  clears the view (the files keep everything).
- `NotificationCenter`: what the operator must see (device failures, capture results, refused actions); shown in
  the status bar with an unread count, listed in a dialog, and logged.
- `StatusBar`: one segment each. Camera: name, mode, rate, lost frames, open / streaming / stopped with the error.
  Mount: the simulated mount under gamepad control, otherwise "none". Sensors, AI, remote: "none" / "not available
  (model stage F)" / "local only" until their stages. Disk: free and total space of the session folder every 5 s,
  warning below 2 GiB.

## Work log
1. `console_panel.cpp`, `status_bar.cpp`; the main window feeds camera state changes and notifications.

## Verification
- `ui.the console shows what is logged, filtered by level and text`; `ui.the log model keeps at most its capacity`.
- `ui.notifications are counted until read and the status bar reports every segment`: segment texts and states,
  disk space of a folder that does not exist yet (the nearest ancestor decides), the warning threshold.
- `ui.the window starts honest…` (P033): none / not available at start.
- Manual: opening and streaming the B0268 moves the camera segment through open → streaming with the rate; pulling
  the cable shows the stop with the error and a notification.

## Exit criteria
- [x] All states visible and accurate (tests above; the mount, sensor, AI and remote segments report absence).

## Safety & failure-mode notes
- The console never blocks logging: the listener only queues; a flood is batched.

## Deviations & next phase
- None. Next: P043 — Input devices.
