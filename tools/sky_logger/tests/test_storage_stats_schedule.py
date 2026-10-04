import csv
import hashlib
from datetime import datetime, timedelta, timezone

import cv2
import numpy as np
import pytest

from sky_logger.cli import next_deadline
from sky_logger.stats import frame_stats
from sky_logger.storage import (append_capture_log, atomic_write_bytes, frame_dir, frame_stem,
                                validate_site)


# ---------------------------------------------------------------- storage
def test_frame_stem_is_utc_and_sortable():
    ist = timezone(timedelta(hours=5, minutes=30))
    t = datetime(2026, 10, 4, 12, 0, 15, 123456, tzinfo=ist)  # 06:30:15.123 UTC
    assert frame_stem("BLR01", t) == "BLR01_20261004T063015_123Z"
    assert frame_dir(__import__("pathlib").Path("/x"), "BLR01", t).name == "2026-10-04"


def test_frame_dir_uses_utc_day():
    ist = timezone(timedelta(hours=5, minutes=30))
    t = datetime(2026, 10, 5, 2, 0, tzinfo=ist)  # still 4 Oct in UTC
    assert frame_dir(__import__("pathlib").Path("/x"), "S", t).name == "2026-10-04"


@pytest.mark.parametrize("bad", ["", "has space", "x" * 33, "../evil", "a/b"])
def test_validate_site_rejects(bad):
    with pytest.raises(ValueError):
        validate_site(bad)


def test_atomic_write_leaves_no_partial(tmp_path):
    p = tmp_path / "d" / "f.bin"
    atomic_write_bytes(p, b"hello")
    assert p.read_bytes() == b"hello"
    assert not list(tmp_path.rglob("*.part"))


def test_capture_log_header_written_once(tmp_path):
    p = tmp_path / "capture_log.csv"
    append_capture_log(p, {"utc": "a", "file": "f1"})
    append_capture_log(p, {"utc": "b", "file": "f2"})
    rows = list(csv.DictReader(open(p, encoding="utf-8")))
    assert [r["file"] for r in rows] == ["f1", "f2"]


# ---------------------------------------------------------------- stats
def test_stats_finds_sun_blob_and_clipping():
    img = np.full((2000, 3000, 3), 100, np.uint8)
    cv2.circle(img, (2400, 500), 120, (255, 255, 255), -1)
    s = frame_stats(img)
    assert s["stats_resolution"] == [1024, 683]
    cx, cy = s["largest_clipped_blob"]["centroid_xy"]
    assert abs(cx - 2400) < 6 and abs(cy - 500) < 6          # full-resolution coordinates
    expected = np.pi * 120**2 / (2000 * 3000)
    assert abs(s["clipped_fraction"] - expected) / expected < 0.05
    assert s["dark_fraction"] == 0.0


def test_stats_no_clipping():
    s = frame_stats(np.full((100, 100, 3), 50, np.uint8))
    assert s["largest_clipped_blob"] is None and s["clipped_fraction"] == 0.0
    assert s["mean_rgb"] == [50.0, 50.0, 50.0]


def test_stats_rejects_wrong_input():
    with pytest.raises(ValueError):
        frame_stats(np.zeros((10, 10), np.uint8))


# ---------------------------------------------------------------- schedule
def test_schedule_on_time_has_no_drift():
    d = 0.0
    for k in range(1, 1001):
        d, skipped = next_deadline(d, now=d - 0.001, interval=30.0)
        assert skipped == 0
    assert d == 30.0 * 1000


def test_schedule_skips_missed_slots_instead_of_bursting():
    d, skipped = next_deadline(0.0, now=95.0, interval=30.0)   # slots at 30, 60, 90 missed
    assert (d, skipped) == (120.0, 3)
    d, skipped = next_deadline(0.0, now=30.0, interval=30.0)   # exactly on the next slot counts as missed
    assert (d, skipped) == (60.0, 1)
