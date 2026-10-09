# The camera tool

`cloudscope-camtool` is the camera subsystem from the command line: everything the desktop application will do
with a camera (P033 onwards) is available here first, for measuring a camera, recording, running capture plans and
calibrating. It lives in `build/<preset>/bin/<Config>/`.

Every command takes `--config FILE` (a configuration file on top of the standard ones) and `--timeout MS` (how long
to wait for a frame; default 2000). Commands that stream take `--mode WxH@FPS/FORMAT` (for example
`1920x1080@30/MJPEG`); without it the camera's current mode is used. Exit codes: 0 success, 1 the command failed or
a check was not met, 2 wrong usage.

## Finding and measuring a camera

| Command | What it does |
|---|---|
| `list` | Every usable camera with its id (`uvc:0c45:636d:1`, `sim:camera:sky`), name and whether it is simulated |
| `caps <id>` | Modes and controls of a camera, with ranges, steps, units and the current values |
| `measure <id> [--seconds S]` | Streams each mode for S seconds and reports the delivered rate and lost frames |
| `stream <id> [--mode M] [--seconds S]` | One mode through the acquisition thread: rate, losses, latency, decode time, luma, memory |
| `set <id> name=value ...` | Sets controls (`exposure=15.6`, `gain=auto`) and prints what the driver made of them |
| `exposure-test <id> [--values LIST]` | Mean luma for each exposure in the list: does the exposure control respond? |
| `decode-bench [--repeat N]` | Decode time per pixel format on simulated frames |
| `ae-test <id> [--seconds S]` | The camera's automatic exposure against CloudScope's sky controller on the same scene |
| `soak <id> [--minutes M] [--report FILE]` | A long run with rate, latency and memory sampled every minute, and a Markdown report |
| `dark\|flat <id> --frames N --out FILE.tiff [--dark FILE]` | Dark or flat master frames (P026) |

## Recording

`record <id> --out FOLDER [--format png|tiff16|jpeg|fits] [--site LAT,LON,ALT[,ID]]` takes one picture with its
sidecar. `sequence` runs a capture plan:

```bash
cloudscope-camtool sequence uvc:0c45:636d:1 --mode 1920x1080@30/MJPEG --kind interval --count 0 --interval 30000 --format jpeg --site 12.97,77.59,920,blr-roof --session D:/sky
```

| Option | Meaning |
|---|---|
| `--kind single\|burst\|interval\|bracket\|scheduled` | What to take (default `interval`) |
| `--count N` | Pictures to take; 0 = until stopped, `--end` or the disk guard |
| `--interval MS` | Time between pictures; the schedule is fixed-rate, late slots are skipped and counted |
| `--format`, `--night-format` | Picture format of the day and night profiles |
| `--night-exposure MS`, `--night-below DEG` | Fixed exposure of the night profile; the Sun elevation below which it applies (default −6°, civil dusk) |
| `--stops LIST` | Bracket stops around the current exposure (default `-2,0,2`) |
| `--start TIME`, `--end TIME` | ISO 8601 with offset, e.g. `2026-10-10T00:30:00Z` |
| `--site LAT,LON,ALT[,ID]` | Where the camera is: enables the Sun position in sidecars and the day/night switch |
| `--template T` | File name template; tokens `{site} {camera} {utc} {date} {seq} {profile} {kind}`; default `{site}_{utc}_{seq}_{profile}` |
| `--min-free MIB` | Disk guard: the run stops below this much free space (default 512 MiB) |
| `--out FOLDER` or `--session ROOT` | Where pictures go: a plain folder, or a new session under ROOT (`<ROOT>/<site>/<date>/<session-id>/frames/`) whose pictures are also entered into `<ROOT>/catalogue.sqlite` |

With MJPEG cameras and `--format jpeg` the camera's own JPEG is stored unchanged. Every picture has a `.json`
sidecar (schema `cloudscope.frame/1`) with the time, the controls the driver reported, site, Sun, statistics and
file hash. If the camera fails during a run the tool reopens it with increasing waits and carries on; the summary
says how many pictures were written, failed, skipped and recovered.

## Calibrating

`calibrate <id> --out model.json [--board 9x6] [--square 25] [--lens fisheye|pinhole] [--views 15] [--seconds 120]`
looks for a checkerboard (9×6 inner corners by default) in the live picture once a second, keeps the views that
cover new parts of the image, and fits the lens model when enough views are in. Move the board to every edge and
tilt it. `--from FOLDER` uses pictures on disk instead of the camera. The result (`cloudscope.camera_model/1`) is the
file STRATIA and CloudScope's overlays read; an rms reprojection error below 0.5 px is good, below 1 px acceptable.
