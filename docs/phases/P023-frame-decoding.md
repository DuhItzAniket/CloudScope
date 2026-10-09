# P023 — Frame decoding

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Turn what a camera delivers (MJPEG, YUYV, BGR/RGB, grey 8/16 bit) into OpenCV images for statistics, display and
recording, fast enough for the camera's rate on the laptop.

## Requirements covered
Groundwork for FR-DSP-01 (live image), FR-REC-01 (pictures in standard formats) and NFR-PERF-01/02.

## Design notes
`core/include/cloudscope/capture/decode.hpp`:
- `jpeg_info(bytes)`: size and subsampling of a JPEG header (TurboJPEG `tjDecompressHeader3`) without decoding;
- `decode_native(frame)`: the frame as an OpenCV image in its own layout (BGR, RGB, grey 8/16, YUYV kept as a
  two-channel 8-bit image, MJPEG decoded to BGR);
- `decode_bgr8(frame)` and `decode_gray8(frame)`: what consumers want (YUYV through OpenCV's
  `COLOR_YUV2BGR_YUYV`, limited-range as UVC cameras send it; 16-bit grey scaled by 1/256).
- One TurboJPEG handle per thread (`thread_local`), so decoding from the acquisition thread and from a recorder
  never contend; the TurboJPEG 2.x API is used because Debian 13 ships libjpeg-turbo 2.1.
- `cloudscope-camtool decode-bench [--repeat N]` times each decoder on frames of the simulated camera.

## Work log
1. Module and tests (`tests/unit/test_decode_statistics.cpp`: every pixel format round-trips through the
   simulated camera's frames; YUYV matches the limited-range formula; a JPEG header is read without decoding;
   damaged bytes give `Parse`).
2. Decode benchmark on the laptop (`build/p023_decode.log`).

## Verification
Median of 20 decodes per mode, simulated frames (synthetic sky), Debug build, development laptop:

| Mode | Frame bytes | Decode (ms) | MPixel/s |
|---|---|---|---|
| 640x480 BGR8 | 921,600 | 0.06 | 5,242 |
| 640x480 RGB8 | 921,600 | 0.77 | 397 |
| 640x480 GRAY8 | 307,200 | 0.50 | 618 |
| 640x480 GRAY16 | 614,400 | 2.15 | 143 |
| 640x480 YUYV | 614,400 | 1.13 | 271 |
| 640x480 MJPEG | 24,645 | 0.87 | 351 |
| 1280x720 MJPEG | 65,632 | 2.54 | 362 |
| 1920x1080 MJPEG | 136,916 | 5.67 | 366 |
| 1920x1080 BGR8 | 6,220,800 | 1.46 | 1,418 |
| 3840x2160 BGR8 | 24,883,200 | 16.23 | 511 |

On the B0268 stream (P022 runs): 1080p MJPEG frames decoded in 4–8 ms, 16 MP MJPEG frames in 33–73 ms (dark
frames, 41 kB and 319 kB), both well inside the frame period (33 ms at 30 fps; 100 ms at 10 fps). These are Debug
numbers; the Release build is faster (P032 reports it).

## Exit criteria
- [x] 1080p MJPEG decodes in under a frame period on the laptop (5.7 ms median against 33 ms).

## Risks / notes
- GRAY16 and RGB8 go through OpenCV conversions that are slow in Debug; none of the B0268's modes uses them.
- Raspberry Pi 5 timings are not measured (no Pi in this stage); NFR-PERF-01 for the Pi is P096's.

## Next phase
P024 — Frame statistics.
