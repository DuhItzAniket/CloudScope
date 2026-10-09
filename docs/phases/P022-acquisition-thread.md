# P022 — Acquisition thread

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
One thread that reads frames from a camera into pooled buffers, stamps them, counts what the camera lost and hands
them to everyone else through a `FrameHub`, without ever waiting for a consumer.

## Requirements covered
FR-CAM-08 (UTC + monotonic timestamp per frame, dropped-frame count), architecture 4.1/4.2 (one producer, no
back-pressure on acquisition), groundwork for NFR-PERF-01.

## Design notes
`core/include/cloudscope/capture/acquisition.hpp`:
- `Acquisition(camera, hub, clock, options)`; `start()` creates a `FramePool` (default 6 frames) sized for the
  camera's current mode, starts the camera's stream and the thread; `stop()` ends both and joins.
- The loop reads into a pooled frame when one is free, otherwise into a scratch buffer that is counted as a
  `pool_miss` and not published: the camera keeps flowing whatever the consumers do.
- `lost` is the sum of gaps in the camera's sequence numbers (the driver's own loss accounting, P019); `timeouts`
  are reads that returned nothing within `read_timeout` (default 500 ms) while the stream stayed alive; an `Io`
  error ends the loop and is kept in `stats().last_error`, so that the owner can reopen the camera (FR-SEQ-05 is
  the sequencer's job, P029).
- Latency = publication time − the frame's arrival stamp (the camera driver stamps on arrival; the hub's
  `publish()` never allocates pixels).
- `cloudscope-camtool stream <id> [--mode] [--seconds]` prints a line per second (fps, lost, timeouts, misses,
  latency, decode time, luma, clipped, resident memory) and a summary.

## Work log
1. Module and tests (`tests/unit/test_acquisition.cpp`: ordered delivery, lost-frame counting, pool misses with a
   slow consumer, `Io` ends the loop with the error kept).
2. camtool `stream` command; two runs on the Arducam B0268 (`build/p022_stream.log`, `build/p022_stream16mp.log`).

## Verification
Simulated camera (no real-time pacing): every published frame arrives in order; a rig-injected loss of frames
shows as `lost`; a consumer that holds every buffer produces pool misses while frames keep being read; pulling the
cable ends acquisition with `ErrorCode::Io`.

Arducam B0268, 10 s runs on the development laptop (Debug build):

| Mode | Frames | Mean fps | Steady fps | Lost | Timeouts | Pool misses | Latency mean / max | Resident memory |
|---|---|---|---|---|---|---|---|---|
| 1920x1080@30 MJPEG | 277 | 26.9 (start-up included) | 28.0–28.3 | 0 | 0 | 0 | 0.3 / 0.9 ms | 72 MiB |
| 4656x3496@10 MJPEG | 112 | 8.9 | 9.6–9.8 | 0 | 0 | 0 | 1.1 / 2.3 ms | 357 MiB |

The 16 MP mode takes about 2 s to deliver its first frame (the camera reconfigures), hence the lower mean. The
decode time column of `stream` is the mean per-frame decode+statistics time over the run; its rise over the run
(4.4 → 7.9 ms at 1080p) is the running mean settling, not a leak (memory stayed flat at 72.2 MiB). The frames in
these runs were dark (luma 0.0: lens covered); the timing does not depend on the scene.

## Exit criteria
- [x] 1080p at the camera's rate with zero lost frames and sub-millisecond publication latency.

## Risks / notes
- Media Foundation delivers at 28 fps for the "30 fps" mode of the B0268 (P020 measured the same); not a loss.
- Pool misses would appear with a consumer slower than the camera that keeps frames (a recorder writing 16 MP
  PNGs); the sequencer therefore takes `Delivery::Latest` for interval runs (P029).

## Next phase
P023 — Frame decoding.
