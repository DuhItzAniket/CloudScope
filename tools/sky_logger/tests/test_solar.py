import math
import random
from datetime import datetime, timedelta, timezone

import pytest

from sky_logger.solar import solar_position


def test_rejects_naive_datetime():
    with pytest.raises(ValueError):
        solar_position(datetime(2026, 10, 4, 6, 0), 12.97, 77.59)


def test_rejects_bad_coordinates():
    t = datetime(2026, 10, 4, 6, 0, tzinfo=timezone.utc)
    with pytest.raises(ValueError):
        solar_position(t, 91.0, 0.0)
    with pytest.raises(ValueError):
        solar_position(t, 0.0, 181.0)


def test_timezone_invariance():
    t = datetime(2026, 10, 4, 6, 30, tzinfo=timezone.utc)
    ist = t.astimezone(timezone(timedelta(hours=5, minutes=30)))
    a, b = solar_position(t, 12.97, 77.59), solar_position(ist, 12.97, 77.59)
    assert a == b


def test_matches_pvlib_spa():
    """Compare against pvlib's NREL SPA over random times and places (2000-2050)."""
    pd = pytest.importorskip("pandas")
    pvsol = pytest.importorskip("pvlib.solarposition")
    rng = random.Random(42)
    base = datetime(2000, 1, 1, tzinfo=timezone.utc)
    max_el, max_az, n_az = 0.0, 0.0, 0
    for _ in range(500):
        t = base + timedelta(seconds=rng.uniform(0, 50 * 365.25 * 86400))
        lat, lon = rng.uniform(-66, 66), rng.uniform(-180, 180)
        ours = solar_position(t, lat, lon)
        ref = pvsol.get_solarposition(pd.DatetimeIndex([t]), lat, lon, method="nrel_numpy").iloc[0]
        max_el = max(max_el, abs(ours.elevation - ref["elevation"]))
        if ref["elevation"] < 89.0:  # azimuth is ill-conditioned at the zenith
            d = abs(ours.azimuth - ref["azimuth"]) % 360.0
            max_az = max(max_az, min(d, 360.0 - d) * math.cos(math.radians(ref["elevation"])))
            n_az += 1
    assert n_az > 400
    assert max_el < 0.02, f"max elevation error {max_el:.4f} deg"
    assert max_az < 0.02, f"max azimuth error (on-sky) {max_az:.4f} deg"
