# Interim Sky Logger (P003)

A small, reliable command-line logger that captures sky frames at a fixed interval, with **UTC timestamps** and a **JSON sidecar per frame** recording what the camera driver actually reported. It exists so the Arducam B0268 can start collecting data for [STRATIA](https://github.com/DuhItzAniket/STRATIA) now, long before the full CloudScope capture system (Stage C) is ready. It runs on Windows laptops and on a Raspberry Pi 5.

## Install

**Windows / Linux laptop**
```
cd tools/sky_logger
python -m venv .venv
.venv\Scripts\activate            # Linux: source .venv/bin/activate
pip install -r requirements.txt   # add -dev to run the tests
```

**Raspberry Pi OS (Bookworm or Trixie)**
```
sudo apt install python3-opencv python3-numpy
cd tools/sky_logger               # run with: python3 -m sky_logger ...
```

## Use it in three steps

**1. Find the camera.** Indices change when cameras are plugged in or out, so always check:
```
python -m sky_logger list
```

**2. Probe it.** Shows which settings the driver accepted and what it reports back:
```
python -m sky_logger probe --camera 1 --fourcc MJPG --width 4656 --height 3496
python -m sky_logger probe --camera 1 --test-exposure -10 -8 -6 -4
```
`--test-exposure` switches to manual exposure, sweeps the values, and tells you whether exposure control really works (brightness must rise with exposure and be reproducible). Afterwards it **restores** the original exposure and switches automatic exposure back on (`--keep-manual` to stay manual). On Windows, `--settings-dialog` opens the driver's own settings window.

**3. Run it.**
```
python -m sky_logger run --camera 1 --fourcc MJPG --width 4656 --height 3496 ^
    --out E:\CloudScopeCaptures --site BLR01 --interval 30 ^
    --lat 12.97 --lon 77.59 --alt 920 --min-sun-elevation -6 ^
    --pointing "zenith, fixed mount" --pointing-el 90
```
(`^` continues a line in Windows `cmd`; use `\` on Linux.) Stop with Ctrl+C; the session record is closed properly.

## Recommended settings for STRATIA data

- **Point the camera straight up (zenith) and keep it fixed.** Record that with `--pointing` / `--pointing-el 90`. STRATIA's cloud-base-height head is trained on zenith views.
- **Use `--min-sun-elevation -6`** (civil twilight) so the logger pauses at night instead of filling the disk with black frames.
- **Always give `--lat/--lon`**, so every sidecar contains the Sun's position.
- **Exposure:** until CloudScope's sky auto-exposure arrives (P025), automatic exposure is acceptable because every sidecar records the exposure the driver reported. If `--test-exposure` shows that manual exposure works on the B0268, a fixed exposure gives more consistent data; the Sun will saturate, which is expected.
- **Storage:** a 16 MP JPEG is roughly 1–3 MB. At one frame per 30 s for 13 daylight hours that is about 2–4 GB per day, so a 1 TB drive holds several months. The exact size per frame is measured once the B0268 is connected.

## Important: camera settings persist

UVC camera drivers **keep exposure, gain and white balance after the program exits**. A logger started without exposure options inherits whatever the last program (this tool, AMCap, SharpCap…) left behind. Each sidecar records the read-back values, so this is visible in the data, but check `probe` before a long run.

## What gets written

```
<out>/<site>/
├── <YYYY-MM-DD>/                       one folder per UTC day
│   ├── <site>_<YYYYMMDDTHHMMSS>_<ms>Z.jpg
│   └── <site>_<YYYYMMDDTHHMMSS>_<ms>Z.json     sidecar
├── sessions/session_<start>Z.json      arguments, host, camera report, counts, stop reason
├── capture_log.csv                     one row per frame (quick overview)
└── logger.log                          rotating text log
```

Files are written to a temporary `.part` file and renamed, so a crash or power cut never leaves a half-written image under its final name.

**Sidecar (`cloudscope.sky_logger.frame/1`)**

| Field | Contents |
|---|---|
| `file`, `sha256`, `bytes`, `encoding` | image file name, checksum, size, format/quality |
| `capture` | `utc` (ISO 8601, ms), `utc_unix`, host monotonic time, sequence number, latency, flushed frames, time source |
| `site` | id, latitude, longitude, altitude |
| `pointing` | operator-declared description, azimuth, elevation (`source: declared_by_operator`, not measured) |
| `camera` | index, backend, requested settings, **read-back of every control**, exposure in seconds when known |
| `image` | width, height, channels |
| `stats` | mean RGB, luma percentiles, clipped and dark fractions, sharpness, largest clipped blob (usually the Sun) in full-resolution pixels; computed on a ≤1024 px copy |
| `sun` | elevation, apparent elevation, azimuth (NOAA algorithm; within 0.02° of NREL SPA) |
| `host`, `notes` | machine, versions, free-text note |

## Behaviour you can rely on

| Situation | What happens |
|---|---|
| Capture slower than the interval | Missed slots are skipped, never captured late in a burst; counted in the session file |
| Camera unplugged / grabs fail | After 3 failures the camera is reopened with backoff (1 s doubling to 60 s) |
| Requested pixel format gives no frames | Reopened with the driver default; recorded as a `fallback` in the session file |
| Disk below `--min-free-gb` (default 5 GB) | Stops cleanly, exit code 3 |
| Sun below `--min-sun-elevation` | Captures pause and resume automatically |

Exit codes: `0` ok, `2` camera error, `3` disk guard, `4` usage error.

## Raspberry Pi: run as a service

Copy [`sky-logger.service`](sky-logger.service) to `/etc/systemd/system/`, edit the paths and options, then:
```
sudo systemctl daemon-reload
sudo systemctl enable --now sky-logger
journalctl -u sky-logger -f
```
For an external drive, mount it at a fixed path with `nofail` in `/etc/fstab` (exFAT works on both Windows and the Pi), and use a powered USB hub if a portable hard disk disconnects.

## Tests

```
pip install -r requirements-dev.txt
python -m pytest tests
```
The solar-position test compares against pvlib's NREL SPA over 500 random times and places.
