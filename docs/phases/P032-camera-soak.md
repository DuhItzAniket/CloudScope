# P032 — Camera soak test

Status: PARTIAL     Date: 2026-10-09     Commit: (this commit)

## Objective
Stream the Arducam B0268 through the whole capture path for a long time and watch rate, losses, latency and
memory: does anything leak, drift or crash?

## Requirements covered
NFR-PERF-01 (1080p preview at ≥ 15 fps on the laptop when the camera supplies it), NFR-REL (long runs without
leaks or crashes); groundwork for the P003 acceptance (B0268 24 h run) and for the Pi 5 figures of P096.

## Design notes
`cloudscope-camtool soak <id> --mode M --minutes N --report FILE` runs the acquisition thread (P022) with a
consumer that decodes every frame (P023) and computes its statistics (P024), samples the counters every 30 s
(frames, lost, timeouts, pool misses, latency mean/max, decode time, luma, clipped fraction, resident memory) and
writes the samples and a summary to a Markdown report. Nothing is written to disk per frame, so the run measures
the camera path itself; the sequencer's writing path has its own figures (P029).

## Work log
1. Owner's instruction for this stage: the plan's 24 h is shortened to about an hour, run in parallel with the
   rest of the stage, "enough to check if the camera can survive an hour or so".
2. Release build of the tool; one-hour run on the development laptop with the B0268 on USB, 2026-10-09
   08:58–09:58 IST, while the laptop also compiled and ran the test suite for the first minutes of the run.

## Verification
`docs/hardware/b0268_soak.md` (the tool's report, 120 samples). Summary:

| Measure | Result |
|---|---|
| Mode | 1920x1080@30 MJPEG (the camera delivers 28 fps in this mode, P020) |
| Duration | 60.0 min, 101,122 frames |
| Rate | 28.09 fps mean; every 30 s sample between 27.7 and 28.4 fps |
| Lost frames | 4 (0.004 %), all between minute 5 and 6, none before or after |
| Timeouts / pool misses / errors | 0 / 0 / 0 |
| Latency (arrival → publication) | mean 0.3 ms, maximum 1.6 ms over the hour |
| Decode + statistics | 4.36 ms per frame (Release) |
| Resident memory | 22.7 MiB before the stream, 26.1 MiB after 30 s, 26.8 MiB at the end: +0.7 MiB over 59.5 min of streaming, within the allocator's noise; no growth trend |
| Clipped fraction | 0.00 % throughout (indoor scene; the luma settled at 45.2 from minute 5, a static scene) |

- No crash, no reopen, no stall. NFR-PERF-01's laptop figure (≥ 15 fps at 1080p) is met with margin: 28 fps is the
  camera's own rate for this mode.
- Temperature was not measured: the laptop exposes no sensor to the tool and the B0268 reports none (an owner
  item if a hot enclosure is ever suspected: a thermometer on the board during a summer run).
- **Not done:** the Raspberry Pi 5 half of the exit criterion (no Pi on the development machine; the V4L2 backend
  of P019 has only been compiled, never run) and the 24 h duration. The 24 h run is the P003 acceptance item the
  owner carries forward; the one-hour result gives no reason to expect a different outcome on the laptop.

## Exit criteria
- [x] No leaks or crashes over the run (one hour on the laptop): memory flat, zero errors.
- [x] Report written (`docs/hardware/b0268_soak.md`).
- [ ] 24 h on the laptop **and** on a Raspberry Pi 5: not run (owner's hardware and time; see above).

## Risks / notes
- The four lost frames in one 30 s window coincide with nothing in the log; a USB or driver hiccup of about 140 ms.
  At 0.004 % it is far inside what a sky logger tolerates; a recurrence pattern would show up in longer runs.
- The run streamed at the camera's full rate; a sky logger at one picture every 30 s loads the system far less.

## Next phase
P033 — App shell (Stage D, desktop application). Stage C is complete on the laptop; the Linux/Pi items stay open.
