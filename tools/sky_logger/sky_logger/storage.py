"""File naming, atomic writes, disk guard and the per-session capture log."""

from __future__ import annotations

import csv
import hashlib
import json
import os
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path

_SITE_RE = re.compile(r"^[A-Za-z0-9_-]{1,32}$")

CAPTURE_LOG_FIELDS = [
    "utc", "file", "sha256", "bytes", "luma_p50", "clipped_fraction",
    "sun_elevation_deg", "exposure_readback", "gain_readback",
]


def validate_site(site: str) -> str:
    if not _SITE_RE.match(site):
        raise ValueError("site must be 1-32 characters of letters, digits, '_' or '-'")
    return site


def frame_stem(site: str, t_utc: datetime) -> str:
    """e.g. BLR01_20261004T063015_123Z — sortable, UTC, millisecond resolution."""
    t = t_utc.astimezone(timezone.utc)
    return f"{site}_{t:%Y%m%dT%H%M%S}_{t.microsecond // 1000:03d}Z"


def frame_dir(root: Path, site: str, t_utc: datetime) -> Path:
    """One folder per UTC day: <root>/<site>/<YYYY-MM-DD>/."""
    return root / site / f"{t_utc.astimezone(timezone.utc):%Y-%m-%d}"


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Write to a temporary file in the same folder, fsync, then rename into place."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def atomic_write_json(path: Path, obj: dict) -> None:
    atomic_write_bytes(path, (json.dumps(obj, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def free_gb(path: Path) -> float:
    path.mkdir(parents=True, exist_ok=True)
    return shutil.disk_usage(path).free / 1e9


def append_capture_log(path: Path, row: dict) -> None:
    """Append one row to capture_log.csv, writing the header if the file is new."""
    new = not path.exists()
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=CAPTURE_LOG_FIELDS, extrasaction="ignore")
        if new:
            w.writeheader()
        w.writerow(row)
