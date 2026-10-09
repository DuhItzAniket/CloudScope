# P029 — Capture sequencer

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Single pictures, bursts, interval series, exposure brackets and scheduled runs, written as pictures with sidecars;
day and night profiles switched at a configurable Sun elevation; file names from a template; a guard against
filling the disk; recovery after a camera failure.

## Requirements covered
FR-SEQ-01 (single, burst, bracket, interval), FR-SEQ-02 (fixed-rate schedule, missed slots skipped and counted),
FR-SEQ-03 (stop on count, duration, end time; pause/resume on Sun elevation), FR-SEQ-05 (reopen the camera with
exponential backoff), FR-SEQ-06 (day/night profiles at a Sun elevation), FR-REC-06 (sortable names with site id
and UTC to the millisecond from a template), FR-REC-07 (disk guard). FR-SEQ-04 (progress display) is the capture
panel's (P039); the command-line tool prints progress per picture.

## Design notes
`core/include/cloudscope/capture/sequencer.hpp`:
- `CapturePlan`: kind, count (0 = open-ended), interval, bracket stops, start/end time, duration, the two
  `CaptureProfile`s (format, JPEG quality, fixed exposure/gain or automatic exposure, native JPEG pass-through),
  the Sun elevation below which the night profile applies (default −6°, civil dusk), an optional pause elevation,
  folder, file name template, disk guard bytes, frame timeout, settling frames, failure limit. `validate()` refuses
  what cannot run before anything is touched.
- `Sequencer(camera, hub, clock)` is a consumer of the acquisition's `FrameHub` like any other (architecture 4.1):
  the preview can run beside it. `Delivery::Latest` for paced runs (the newest frame at each slot), `Queue` for
  bursts (consecutive frames). It touches camera controls only for a profile's fixed exposure/gain, for brackets,
  and when a profile asks for automatic exposure; after a control change `settle_frames` frames are skipped.
- Schedule: anchored to the first picture; a slow picture does not shift the series, and slots that passed while it
  was taken are skipped and counted in `missed_slots`, never taken late in a burst (FR-SEQ-02, the interim
  logger's rule).
- Profile choice at every picture from the Sun's elevation at the site (`solar.hpp`) and the plan's threshold; a
  switch applies the new profile's controls and counts in `profile_switches`. Without a site the day profile is
  used.
- Recovery (FR-SEQ-05): when no frame comes and the camera reports it is no longer streaming, the owner's
  `Recovery` callback (close/open the camera, set its mode, restart the acquisition on the same hub) is called with
  backoff 0.5 s doubling to 60 s until it succeeds or the run is stopped; the number of recoveries is reported.
  Without a callback the run ends with `stopped_by_failures`, as it does after `max_consecutive_failures` (3)
  frame failures in a row.
- End conditions: count, `end_at`, `duration`, the disk guard (`std::filesystem::space` below `min_free_bytes`,
  checked before every picture), `request_stop()` from any thread (honoured within 50 ms, also while waiting for a
  scheduled start or paused for the Sun).
- File names: `expand_filename()` with tokens `{site} {camera} {utc} {date} {seq} {profile} {kind}`; default
  `{site}_{utc}_{seq}_{profile}` → `blr-roof_20261009T101530_123Z_000042_day.jpg`: sortable, site and UTC to the
  millisecond (FR-REC-06). Unknown tokens and unbalanced braces are rejected at validation.
- `cloudscope-camtool record|sequence` (manual: `docs/manual/camera_tool.md`) run plans from the command line,
  with `--site` for the Sun, `--session ROOT` to write into a session with a catalogue (P030), and a recovery
  callback that reopens the camera.

## Work log
1. Module, tests (`tests/unit/test_sequencer.cpp`), camtool commands.
2. Tightened to the SRS: missed-slot accounting, duration, Sun pause, recovery with backoff, failure limit, the
   default template with the site id.
3. Test timings adjusted for a Debug build running beside other tests (a 640x480 picture with sidecar takes tens
   of milliseconds there); the 1,000-frame run became a hidden `[slow]` test with a 100-frame twin in the default
   suite.

## Verification
Simulated sky camera (640x480 MJPEG, no real-time pacing), manual clock set to Bengaluru daytime unless stated:
- Templates expand and reject as specified; plans are validated (burst without count, bracket without stops,
  scheduled without start or ending before its start, bad JPEG quality, negative exposure, unknown token).
- **1,000-frame interval run (exit criterion):** 1,000 JPEG pictures (the camera's own MJPEG bytes) with 1,000
  schema-valid sidecars, no failure, names `000000_day.jpg` … `000999_day.jpg`, hash in the sidecar equal to the
  file's, Sun elevation 36° in the sidecars, no `.part` leftovers; 51.0 s for the 1,000 pictures in the Debug build and 16.3 s in Release (16 ms per picture,
  with the simulated camera rendering on another thread).
  The 100-frame twin runs in the default suite.
- Burst of 10: ten pictures with strictly increasing sequence numbers.
- Cadence: interval 250 ms, a consumer that sleeps 800 ms after the second picture: 5 pictures, 2–4 missed slots,
  later pictures about an interval apart (not a burst).
- **Profile switch (exit criterion):** four pictures; the clock set to 23:00 UTC after the second: pictures 3 and 4
  are `_night.png` with the night profile's 20 ms exposure read back in the sidecar, one settling frame skipped,
  `profile_switches` = 1; `profile_for()` chooses night only below the threshold and day without a site.
- Pause: a run started at night with `pause_below = 0°` writes nothing until the clock is set to day, then
  completes (`pauses` = 1); a paused run still ends on its duration.
- Bracket of stops −2/0/+2 around 10 ms: pictures at 2.5, 10 and 40 ms (read back), the camera back at 10 ms.
- Scheduled: a start in the past runs at once; an end already reached writes nothing; a duration ends an
  open-ended run; a future start ends on `request_stop()`; advancing the clock past the start releases the run.
- Recovery: the simulated cable pulled after the second picture; the first recovery attempt fails, the second
  reconnects and restarts the acquisition; the run completes its 6 pictures with `recoveries` = 1, `failed` = 1.
- Disk guard with an impossible threshold stops at once; a silent camera without recovery ends with
  `stopped_by_failures` and a `Timeout` as the last error; a camera that never comes back ends on
  `request_stop()`; a second `run()` during a run is `Unavailable`.
- Not run on the B0268 in this phase beyond `record` (the soak of P032 streams the camera for an hour; a
  night-long sequence with the real profile switch is the owner's first field session).

## Exit criteria
- [x] 1,000-frame interval run without error (hidden `[slow]` test, run by hand in Debug and Release; see above).
- [x] Profile switch at the configured elevation (test).

## Risks / notes
- The Sun elevation for the profile is computed from the sequencer's clock, the Sun in the sidecar from the frame's
  own timestamp; the two differ by the frame latency only.
- Pictures are written on the sequencer's thread; at 16 MP with PNG this takes longer than a 1 s interval in a
  Debug build (P032 measures Release). The schedule copes (missed slots are counted), but a long interval or JPEG
  pass-through is the right setting for a sky logger.
- "Pause on Sun" uses the geometric elevation, like the thresholds; refraction (0.5° at the horizon) is not applied.

## Next phase
P030 — Sessions & catalogue.
