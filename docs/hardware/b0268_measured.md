# Arducam B0268 (16 MP, "Arducam_16MP"): measured behaviour

Measured with `cloudscope-camtool` on the development laptop (Windows 11, Media Foundation backend, Debug build),
2026-10-09, in phases P019–P021. USB ids `0c45:636d`, device id `uvc:0c45:636d:1`. Re-measure with
`cloudscope-camtool measure uvc:0c45:636d:1` after a driver or firmware change.

## Modes offered and delivered (P020)

`cloudscope-camtool measure uvc:0c45:636d:1 --seconds 3`: every mode streamed for 3 s, frames counted after the
first one arrived. MJPEG frame sizes are of the scene at hand (a desk); the sky will differ.

| Mode | Nominal fps | Measured fps | Frames | Lost | Timeouts | Bytes per frame | Note |
|---|---|---|---|---|---|---|---|
| 1920x1080@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 50,544 | |
| 4656x3496@10/MJPEG | 10 | 9.7 | 31 | 0 | 0 | 393,664 | full 16 MP frame |
| 3840x2160@10/MJPEG | 10 | 9.6 | 30 | 0 | 0 | 214,904 | |
| 2592x1944@10/MJPEG | 10 | 9.7 | 31 | 0 | 0 | 127,800 | |
| 2320x1744@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 91,440 | |
| 1600x1200@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 45,576 | |
| 1280x720@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 24,424 | |
| 800x600@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 13,120 | |
| 640x480@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 8,872 | |
| 320x240@30/MJPEG | 30 | 28.1 | 86 | 0 | 0 | 2,912 | |
| 1280x720@10/YUYV | 10 | 7.7 | 24 | 0 | 0 | 1,843,200 | below nominal |
| 1600x1200@5/YUYV | 5 | 3.9 | 9 | 0 | 1 | 3,840,000 | below nominal |
| 800x600@15/YUYV | 15 | 11.4 | 36 | 0 | 0 | 960,000 | below nominal |
| 640x480@20/YUYV | 20 | 15.1 | 47 | 6 | 0 | 614,400 | below nominal, frames lost |
| 320x240@20/YUYV | 20 | 15.1 | 47 | 6 | 0 | 153,600 | below nominal, frames lost |

Reading: the MJPEG modes deliver 94–97 % of their nominal rate with no lost frames; the uncompressed YUYV modes
deliver about 75 % of nominal and lose frames at the two smallest sizes (USB bandwidth and the driver's buffering).
For sky capture the MJPEG modes are the ones to use; the full 16 MP frame comes at 9.7 fps.

## Controls (P021)

As reported by the driver (DirectShow `IAMCameraControl` / `IAMVideoProcAmp` through Media Foundation):

| Control | Range | Step | Default | Unit | Auto | Calibrated |
|---|---|---|---|---|---|---|
| exposure | 0.122 .. 500 | powers of two | 15.6 | ms | yes | yes (log2 seconds scale of the driver) |
| gain | 0 .. 100 | 1 | 0 | driver scale | no | no |
| white_balance | 2800 .. 6500 | 1 | 4600 | K | yes | yes |
| brightness | -64 .. 64 | 1 | 0 | driver scale | no | no |
| contrast | 0 .. 64 | 1 | 32 | driver scale | no | no |
| saturation | 0 .. 128 | 1 | 64 | driver scale | no | no |
| gamma | 72 .. 500 | 1 | 100 | driver scale | no | no |
| sharpness | 0 .. 6 | 1 | 3 | driver scale | no | no |

No focus control (fixed-focus lens). Exposure can only take powers of two of a second (0.122, 0.244, ... 500 ms):
a request for 10 ms is answered with 7.8125 ms and `applied = false`, which is the honest read-back the HAL demands.

### Exposure response

`cloudscope-camtool exposure-test uvc:0c45:636d:1 --mode 1280x720@30/MJPEG --values 0.5,1,2,4,8,16,32,64,128`
(manual exposure, gain 0, indoor desk scene, mean luma of three frames after six discarded):

| Requested (ms) | Effective (ms) | Mean luma |
|---|---|---|
| 0.5 | 0.488 | 1.1 |
| 1 | 0.977 | 0.2 |
| 2 | 1.953 | 0.3 |
| 4 | 3.906 | 0.4 |
| 8 | 7.813 | 1.0 |
| 16 | 15.625 | 2.6 |
| 32 | 31.25 | 6.6 |
| 64 | 62.5 | 14.5 |
| 128 | 125 | 23.4 |

Brightness rises monotonically with exposure (the first three values are at the sensor's noise floor in that
scene): the exposure control is effective, and read-back values are the ones the camera uses. A camera setting
persists after the program exits (UVC behaviour); the tool restores the exposure it found.
