"""Command-line interface: `list`, `probe` and `run`."""

from __future__ import annotations

import argparse
import json
import logging
import logging.handlers
import platform
import signal
import socket
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import cv2

from . import SESSION_SCHEMA, SIDECAR_SCHEMA, __version__
from .camera import BACKENDS, Camera, CameraConfig, default_backend, list_cameras
from .solar import solar_position
from .stats import frame_stats
from .storage import (append_capture_log, atomic_write_bytes, atomic_write_json, frame_dir,
                      frame_stem, free_gb, sha256_hex, validate_site)

log = logging.getLogger("sky_logger")

EXIT_OK, EXIT_CAMERA, EXIT_DISK, EXIT_USAGE = 0, 2, 3, 4
MAX_CONSECUTIVE_FAILURES = 3
MAX_BACKOFF_S = 60.0


def next_deadline(deadline: float, now: float, interval: float) -> tuple[float, int]:
    """Advance a fixed-rate schedule by one slot without drift.

    If capturing took longer than the interval, missed slots are skipped (not
    captured late in a burst). Returns (new_deadline, skipped_slot_count).
    """
    new = deadline + interval
    if new > now:
        return new, 0
    skipped = int((now - new) // interval) + 1
    return new + skipped * interval, skipped


def host_info() -> dict:
    return {"hostname": socket.gethostname(), "platform": platform.platform(),
            "python": platform.python_version(), "opencv": cv2.__version__, "logger_version": __version__}


def camera_config_from_args(a: argparse.Namespace) -> CameraConfig:
    return CameraConfig(index=a.camera, backend=a.backend, width=a.width, height=a.height,
                        fourcc=None if a.fourcc.lower() == "none" else a.fourcc, fps=a.fps,
                        auto_exposure=a.auto_exposure, exposure=a.exposure, gain=a.gain,
                        auto_wb=a.auto_wb, wb_temperature=a.wb_temperature, flush_frames=a.flush_frames)


# ---------------------------------------------------------------- list / probe
def cmd_list(a: argparse.Namespace) -> int:
    cams = list_cameras(a.backend, a.max_index)
    print(json.dumps(cams, indent=2))
    if not cams:
        print(f"No camera found with backend {a.backend}.", file=sys.stderr)
    return EXIT_OK if cams else EXIT_CAMERA


def _mean_luma(cam: Camera, settle_frames: int, n: int = 3) -> float | None:
    for _ in range(settle_frames):
        cam.cap.read()
    vals = []
    for _ in range(n):
        frame, *_ = cam.grab_fresh()
        if frame is not None:
            vals.append(float(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).mean()))
    return sum(vals) / len(vals) if vals else None


def exposure_test_verdict(luma: list[float | None], repeat_luma: float | None) -> tuple[bool | None, str]:
    """Decide whether manual exposure control works, from a sweep of increasing raw exposure values.

    Larger raw values mean longer exposures on both DirectShow (log2 s) and V4L2 (100 us units),
    so brightness must not fall as the value rises, must rise overall, and re-measuring the first
    value must reproduce its brightness (otherwise auto-exposure or the scene is changing).
    """
    if any(v is None for v in luma) or repeat_luma is None:
        return None, "no frames during test"
    drift = abs(repeat_luma - luma[0])
    if drift > 5.0:
        return False, f"first value re-measured {drift:.1f} levels off: auto-exposure still active or scene changing"
    if any(b - a < -2.0 for a, b in zip(luma, luma[1:])):
        return False, "brightness fell while exposure rose: driver ignores the value or auto-exposure overrides it"
    if luma[-1] - luma[0] < 10.0:
        return False, "brightness barely changed across the sweep"
    return True, "brightness rises monotonically with exposure and is reproducible"


def cmd_probe(a: argparse.Namespace) -> int:
    cfg = camera_config_from_args(a)
    try:
        cam = Camera(cfg)
        cam.open()
    except RuntimeError as e:
        print(str(e), file=sys.stderr)
        return EXIT_CAMERA
    with cam:
        if a.settings_dialog:
            print("Opening driver settings dialog:", "ok" if cam.open_settings_dialog() else "not supported by this backend")
        frame, *_ = cam.grab_fresh()
        report = {
            "host": host_info(),
            "camera_index": cfg.index,
            "backend": cfg.backend,
            "control_report": cam.control_report,
            "readback": cam.read_controls(),
            "frame_delivered": frame is not None,
            "frame_shape": list(frame.shape) if frame is not None else None,
        }
        if a.test_exposure is not None:
            values = sorted(a.test_exposure)
            if len(values) < 3:
                print("--test-exposure needs at least 3 values", file=sys.stderr)
                return EXIT_USAGE
            # UVC drivers keep settings after the program exits, so remember and restore them.
            original_exposure = cam.read_controls()["exposure"]
            cam.cfg.auto_exposure = "manual"
            luma, readback = [], []
            for v in values + [values[0]]:          # sweep upward, then repeat the first value
                cam.cfg.exposure = v
                rep = cam._apply_controls(skip_fourcc=True)
                readback.append(rep.get("exposure", {}).get("readback"))
                luma.append(_mean_luma(cam, a.settle_frames))
            ok, reason = exposure_test_verdict(luma[:-1], luma[-1])
            # Restore in two steps: on DirectShow, writing an exposure value re-enters manual mode,
            # so the value goes first and automatic mode is switched on last (verified in P003).
            cam.cfg.auto_exposure, cam.cfg.exposure = "leave", original_exposure
            restore = cam._apply_controls(skip_fourcc=True)
            if not a.keep_manual:
                cam.cfg.auto_exposure, cam.cfg.exposure = "auto", None
                restore.update(cam._apply_controls(skip_fourcc=True))
            report["exposure_test"] = {
                "values": values, "mean_luma": luma[:-1], "repeat_first_value_luma": luma[-1],
                "readback": readback, "exposure_control_effective": ok, "reason": reason,
                "settle_frames": a.settle_frames,
                "restored": {"exposure": restore.get("exposure"), "auto_exposure": restore.get("auto_exposure")},
            }
    print(json.dumps(report, indent=2, default=str))
    return EXIT_OK if report["frame_delivered"] else EXIT_CAMERA


# ---------------------------------------------------------------- run
class _Stop:
    def __init__(self) -> None:
        self.requested = False
        self.reason = "completed"

    def trigger(self, reason: str) -> None:
        self.requested, self.reason = True, reason


def _setup_file_logging(site_root: Path) -> None:
    site_root.mkdir(parents=True, exist_ok=True)
    fh = logging.handlers.RotatingFileHandler(site_root / "logger.log", maxBytes=5_000_000, backupCount=5, encoding="utf-8")
    fh.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    logging.getLogger().addHandler(fh)


def _sun(a: argparse.Namespace, t: datetime) -> dict | None:
    if a.lat is None or a.lon is None:
        return None
    sp = solar_position(t, a.lat, a.lon)
    return {"elevation_deg": round(sp.elevation, 4), "apparent_elevation_deg": round(sp.apparent_elevation, 4),
            "azimuth_deg": round(sp.azimuth, 4)}


def cmd_run(a: argparse.Namespace) -> int:
    try:
        site = validate_site(a.site)
    except ValueError as e:
        print(e, file=sys.stderr)
        return EXIT_USAGE
    if a.min_sun_elevation is not None and (a.lat is None or a.lon is None):
        print("--min-sun-elevation needs --lat and --lon", file=sys.stderr)
        return EXIT_USAGE
    if a.interval <= 0:
        print("--interval must be positive", file=sys.stderr)
        return EXIT_USAGE

    root = Path(a.out).expanduser().resolve()
    site_root = root / site
    _setup_file_logging(site_root)

    stop = _Stop()
    signal.signal(signal.SIGINT, lambda *_: stop.trigger("interrupted (SIGINT)"))
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda *_: stop.trigger("terminated (SIGTERM)"))

    cfg = camera_config_from_args(a)
    cam = Camera(cfg)
    try:
        cam.open()
    except RuntimeError as e:
        log.error("%s", e)
        return EXIT_CAMERA
    if a.settings_dialog:
        log.info("Driver settings dialog: %s", "opened" if cam.open_settings_dialog() else "not supported")

    started = datetime.now(timezone.utc)
    session_path = site_root / "sessions" / f"session_{started:%Y%m%dT%H%M%SZ}.json"
    session = {
        "schema": SESSION_SCHEMA, "started_utc": started.isoformat(), "ended_utc": None, "stop_reason": None,
        "args": {k: v for k, v in vars(a).items() if k != "func"}, "host": host_info(),
        "camera_control_report": cam.control_report, "camera_readback_at_start": cam.read_controls(),
        "counts": {"captured": 0, "failed_grabs": 0, "skipped_slots": 0, "night_skips": 0, "reconnects": 0},
    }
    atomic_write_json(session_path, session)
    log.info("Session %s -> %s (interval %.1fs)", session_path.name, site_root, a.interval)
    for k, v in cam.control_report.items():
        log.info("control %-14s requested=%s accepted=%s readback=%s", k, v.get("requested"), v.get("accepted"), v.get("readback"))

    counts = session["counts"]
    seq, failures, backoff = 0, 0, 1.0
    deadline = time.monotonic()
    t_end = None if a.duration is None else time.monotonic() + a.duration
    night = False
    exit_code = EXIT_OK

    while not stop.requested:
        now = time.monotonic()
        if now < deadline:
            time.sleep(min(deadline - now, 0.5))
            continue
        if t_end is not None and now >= t_end:
            stop.trigger("duration reached")
            break

        if free_gb(root) < a.min_free_gb:
            log.error("Free disk space below %.1f GB; stopping.", a.min_free_gb)
            stop.trigger("disk guard")
            exit_code = EXIT_DISK
            break

        if a.min_sun_elevation is not None:
            elev = solar_position(datetime.now(timezone.utc), a.lat, a.lon).elevation
            if elev < a.min_sun_elevation:
                if not night:
                    log.info("Sun elevation %.1f deg below %.1f; pausing captures.", elev, a.min_sun_elevation)
                    night = True
                counts["night_skips"] += 1
                deadline, _ = next_deadline(deadline, time.monotonic(), a.interval)
                continue
            if night:
                log.info("Sun elevation %.1f deg; resuming captures.", elev)
                night = False

        frame, t_utc, t_mono, latency_ms = cam.grab_fresh()
        if frame is None:
            failures += 1
            counts["failed_grabs"] += 1
            log.warning("Grab failed (%d consecutive).", failures)
            if failures >= MAX_CONSECUTIVE_FAILURES:
                log.warning("Reopening camera after %.0fs backoff.", backoff)
                cam.release()
                time.sleep(backoff)
                backoff = min(backoff * 2, MAX_BACKOFF_S)
                try:
                    cam.open()
                    counts["reconnects"] += 1
                    failures = 0
                except RuntimeError as e:
                    log.error("%s", e)
            deadline, skipped = next_deadline(deadline, time.monotonic(), a.interval)
            counts["skipped_slots"] += skipped
            continue
        failures, backoff = 0, 1.0

        ext = a.format
        params = [cv2.IMWRITE_JPEG_QUALITY, a.jpeg_quality] if ext == "jpg" else [cv2.IMWRITE_PNG_COMPRESSION, 3]
        ok, buf = cv2.imencode("." + ext, frame, params)
        if not ok:
            log.error("Image encoding failed; frame dropped.")
            counts["failed_grabs"] += 1
        else:
            data = buf.tobytes()
            stem = frame_stem(site, t_utc)
            day_dir = frame_dir(root, site, t_utc)
            img_path = day_dir / f"{stem}.{ext}"
            atomic_write_bytes(img_path, data)
            st = frame_stats(frame)
            readback = cam.read_controls()
            sun = _sun(a, t_utc)
            sidecar = {
                "schema": SIDECAR_SCHEMA,
                "file": img_path.name,
                "sha256": sha256_hex(data),
                "bytes": len(data),
                "encoding": {"format": ext, "jpeg_quality": a.jpeg_quality if ext == "jpg" else None},
                "capture": {"utc": t_utc.isoformat(timespec="milliseconds"), "utc_unix": t_utc.timestamp(),
                            "host_monotonic_s": t_mono, "sequence": seq, "grab_latency_ms": round(latency_ms, 2),
                            "flush_frames": cfg.flush_frames, "time_source": "host clock (not GPS-disciplined)"},
                "site": {"id": site, "latitude": a.lat, "longitude": a.lon, "altitude_m": a.alt},
                "pointing": {"source": "declared_by_operator", "description": a.pointing,
                             "azimuth_deg": a.pointing_az, "elevation_deg": a.pointing_el},
                "camera": {"index": cfg.index, "backend": cfg.backend, "requested": cfg.requested(), "readback": readback},
                "image": {"width": int(frame.shape[1]), "height": int(frame.shape[0]), "channels": int(frame.shape[2])},
                "stats": st,
                "sun": sun,
                "host": host_info(),
                "notes": a.note,
            }
            atomic_write_json(img_path.with_suffix(".json"), sidecar)
            append_capture_log(site_root / "capture_log.csv", {
                "utc": sidecar["capture"]["utc"], "file": img_path.relative_to(root).as_posix(), "sha256": sidecar["sha256"],
                "bytes": len(data), "luma_p50": round(st["luma_p01_p50_p99"][1], 2),
                "clipped_fraction": round(st["clipped_fraction"], 5),
                "sun_elevation_deg": None if sun is None else sun["elevation_deg"],
                "exposure_readback": readback["exposure"], "gain_readback": readback["gain"]})
            counts["captured"] += 1
            seq += 1
            log.info("#%d %s  p50=%.0f clipped=%.3f%s", seq, img_path.name, st["luma_p01_p50_p99"][1],
                     st["clipped_fraction"], "" if sun is None else f" sun_el={sun['elevation_deg']:.1f}")
            if a.count is not None and counts["captured"] >= a.count:
                stop.trigger("count reached")
                break

        deadline, skipped = next_deadline(deadline, time.monotonic(), a.interval)
        if skipped:
            counts["skipped_slots"] += skipped
            log.warning("Capture slower than interval; skipped %d slot(s).", skipped)

    cam.release()
    session["ended_utc"] = datetime.now(timezone.utc).isoformat()
    session["stop_reason"] = stop.reason
    atomic_write_json(session_path, session)
    log.info("Stopped: %s. Counts: %s", stop.reason, counts)
    return exit_code


# ---------------------------------------------------------------- parser
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="sky_logger", description="CloudScope interim sky logger (P003).")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    sub = p.add_subparsers(dest="command", required=True)

    def camera_args(sp: argparse.ArgumentParser) -> None:
        g = sp.add_argument_group("camera")
        g.add_argument("--camera", type=int, default=0, help="camera index (see `list`)")
        g.add_argument("--backend", choices=sorted(BACKENDS), default=default_backend())
        g.add_argument("--width", type=int)
        g.add_argument("--height", type=int)
        g.add_argument("--fourcc", default="none",
                       help="pixel format, e.g. MJPG (recommended for high-resolution USB2 cameras) or YUY2; "
                            "'none' keeps the driver default")
        g.add_argument("--fps", type=float)
        g.add_argument("--auto-exposure", choices=["leave", "manual", "auto"], default="leave")
        g.add_argument("--exposure", type=float, help="raw backend units (DirectShow: log2 seconds, e.g. -7; V4L2: 100 us units)")
        g.add_argument("--gain", type=float)
        g.add_argument("--auto-wb", choices=["leave", "manual", "auto"], default="leave")
        g.add_argument("--wb-temperature", type=float)
        g.add_argument("--flush-frames", type=int, default=3, help="buffered frames discarded before each capture")
        g.add_argument("--settings-dialog", action="store_true", help="open the driver settings dialog (DirectShow)")

    sp = sub.add_parser("list", help="list cameras that deliver frames")
    sp.add_argument("--backend", choices=sorted(BACKENDS), default=default_backend())
    sp.add_argument("--max-index", type=int, default=8)
    sp.set_defaults(func=cmd_list)

    sp = sub.add_parser("probe", help="open a camera, apply settings and report what the driver accepted")
    camera_args(sp)
    sp.add_argument("--test-exposure", type=float, nargs="+", metavar="VALUE",
                    help="switch to manual exposure, sweep >=3 raw values and report whether exposure control really works")
    sp.add_argument("--settle-frames", type=int, default=15, help="frames discarded after each exposure change")
    sp.add_argument("--keep-manual", action="store_true",
                    help="after --test-exposure, restore the original exposure value but stay in manual mode "
                         "(default: switch back to automatic exposure)")
    sp.set_defaults(func=cmd_probe)

    sp = sub.add_parser("run", help="capture frames at a fixed interval")
    camera_args(sp)
    g = sp.add_argument_group("schedule and output")
    g.add_argument("--out", required=True, help="output root folder (e.g. an external drive)")
    g.add_argument("--site", required=True, help="site id used in file names, e.g. BLR01")
    g.add_argument("--interval", type=float, default=30.0, help="seconds between captures")
    g.add_argument("--count", type=int, help="stop after N frames")
    g.add_argument("--duration", type=float, help="stop after N seconds")
    g.add_argument("--format", choices=["jpg", "png"], default="jpg")
    g.add_argument("--jpeg-quality", type=int, default=95)
    g.add_argument("--min-free-gb", type=float, default=5.0, help="stop when free space drops below this")
    g = sp.add_argument_group("site and pointing metadata")
    g.add_argument("--lat", type=float, help="site latitude, deg north")
    g.add_argument("--lon", type=float, help="site longitude, deg east")
    g.add_argument("--alt", type=float, help="site altitude, m above sea level")
    g.add_argument("--min-sun-elevation", type=float, help="pause captures while the Sun is below this elevation (needs --lat/--lon)")
    g.add_argument("--pointing", default="unspecified", help="free-text pointing, e.g. 'zenith, fixed mount'")
    g.add_argument("--pointing-az", type=float, help="declared camera azimuth, deg")
    g.add_argument("--pointing-el", type=float, help="declared camera elevation, deg (90 = zenith)")
    g.add_argument("--note", default="", help="free-text note stored in every sidecar")
    sp.set_defaults(func=cmd_run)
    return p


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "auto_exposure", None) == "auto" and getattr(args, "exposure", None) is not None:
        parser.error("--auto-exposure auto conflicts with --exposure (setting a value switches the camera to manual)")
    return args.func(args)
