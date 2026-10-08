# P020 — Modes

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Negotiate resolution, pixel format and frame rate with a real camera, and measure what the B0268 actually
delivers in every mode.

## Requirements covered
FR-CAM-03 (modes discovered, not assumed), FR-CAM-04 (read-back of the mode in effect).

## Design notes
- Modes come from the camera's native media types (Windows) or `VIDIOC_ENUM_FMT/FRAMESIZES/FRAMEINTERVALS` (Linux);
  only pixel formats CloudScope has a `PixelFormat` for are listed (MJPEG, YUYV, RGB24/BGR, GREY, Y16 on Linux).
  Media Foundation's converters are disabled so that the camera's own formats are seen, not conversions.
- `set_mode()` applies the exact native type and returns what is in effect; the camera's own choice after `open()`
  is reported as the current mode (the B0268 opens in 320x240 YUYV).
- RAW8/RAW16/MONO16, ROI and binning (plan wording): the B0268 is a UVC camera and offers none of these through
  UVC; the mode list is what the device reports, which is the honest answer for this camera. They stay in the
  interface for cameras that have them (V4L2 GREY/Y16 are mapped).

## Work log
1. Mode discovery in both backends; the `measure` command of `cloudscope-camtool`: every mode streamed for N
   seconds, frames counted after the first arrival, lost frames from sequence gaps, bytes per frame.
2. Measured the B0268: `docs/hardware/b0268_measured.md`.

## Verification
`cloudscope-camtool measure uvc:0c45:636d:1 --seconds 3` (Windows, Debug build):

| Mode family | Nominal | Measured | Lost |
|---|---|---|---|
| MJPEG 320x240 … 2320x1744 at 30 fps (7 modes) | 30 | 28.1 fps each | 0 |
| MJPEG 2592x1944, 3840x2160, 4656x3496 at 10 fps | 10 | 9.6–9.7 fps | 0 |
| YUYV 1280x720@10, 1600x1200@5, 800x600@15 | 10 / 5 / 15 | 7.7 / 3.9 / 11.4 fps | 0 |
| YUYV 640x480@20, 320x240@20 | 20 | 15.1 fps | 6 each |

Full table with bytes per frame in `docs/hardware/b0268_measured.md`. Every listed mode could be selected and
streamed (the contract test selects each one); the full 16 MP frame streams at 9.7 fps as MJPEG of about 390 kB.

## Exit criteria
- [x] Table in docs with the actual fps per mode.

## Risks / notes
- YUYV modes run about 25 % below nominal and drop frames at small sizes: use MJPEG for the sky; the decoder path
  (P023) is therefore the one that matters for throughput.
- The measured rates are from a Debug build; the Release soak (P032) confirms them.

## Next phase
P021 — Camera controls.
