"""Solar position from the NOAA Solar Calculator equations (after Meeus).

Accuracy is about 0.01 deg in elevation for years 1800-2100, which is ample for
tagging sky frames and for daylight scheduling. Angles are in degrees, azimuth
is measured clockwise from geographic north.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from datetime import datetime, timezone


@dataclass(frozen=True)
class SolarPosition:
    elevation: float           # true (geometric) elevation, deg
    apparent_elevation: float  # elevation including atmospheric refraction, deg
    azimuth: float             # deg clockwise from north, [0, 360)
    declination: float         # deg
    equation_of_time_min: float


def _julian_day(t: datetime) -> float:
    if t.tzinfo is None:
        raise ValueError("datetime must be timezone-aware (use UTC)")
    return t.timestamp() / 86400.0 + 2440587.5


def _refraction_deg(elevation: float) -> float:
    """NOAA approximation of atmospheric refraction, in degrees."""
    if elevation > 85.0:
        return 0.0
    te = math.tan(math.radians(elevation))
    if elevation > 5.0:
        arcsec = 58.1 / te - 0.07 / te**3 + 0.000086 / te**5
    elif elevation > -0.575:
        arcsec = 1735.0 + elevation * (-518.2 + elevation * (103.4 + elevation * (-12.79 + elevation * 0.711)))
    else:
        arcsec = -20.772 / te
    return arcsec / 3600.0


def solar_position(t: datetime, latitude: float, longitude: float) -> SolarPosition:
    """Sun position for a timezone-aware datetime at (latitude, longitude) in degrees, east positive."""
    if not -90.0 <= latitude <= 90.0:
        raise ValueError(f"latitude out of range: {latitude}")
    if not -180.0 <= longitude <= 180.0:
        raise ValueError(f"longitude out of range: {longitude}")

    jc = (_julian_day(t) - 2451545.0) / 36525.0
    mean_long = (280.46646 + jc * (36000.76983 + jc * 0.0003032)) % 360.0
    mean_anom = 357.52911 + jc * (35999.05029 - 0.0001537 * jc)
    ecc = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc)
    m = math.radians(mean_anom)
    eq_center = (math.sin(m) * (1.914602 - jc * (0.004817 + 0.000014 * jc))
                 + math.sin(2 * m) * (0.019993 - 0.000101 * jc)
                 + math.sin(3 * m) * 0.000289)
    true_long = mean_long + eq_center
    omega = math.radians(125.04 - 1934.136 * jc)
    app_long = true_long - 0.00569 - 0.00478 * math.sin(omega)
    mean_obliq = 23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0
    obliq = math.radians(mean_obliq + 0.00256 * math.cos(omega))
    decl = math.asin(math.sin(obliq) * math.sin(math.radians(app_long)))

    y = math.tan(obliq / 2.0) ** 2
    l0 = math.radians(mean_long)
    eot = 4.0 * math.degrees(
        y * math.sin(2 * l0) - 2 * ecc * math.sin(m) + 4 * ecc * y * math.sin(m) * math.cos(2 * l0)
        - 0.5 * y * y * math.sin(4 * l0) - 1.25 * ecc * ecc * math.sin(2 * m))

    tu = t.astimezone(timezone.utc)
    minutes = tu.hour * 60.0 + tu.minute + (tu.second + tu.microsecond / 1e6) / 60.0
    true_solar_time = (minutes + eot + 4.0 * longitude) % 1440.0
    hour_angle = true_solar_time / 4.0 - 180.0  # deg, negative before solar noon

    lat = math.radians(latitude)
    ha = math.radians(hour_angle)
    cos_zen = math.sin(lat) * math.sin(decl) + math.cos(lat) * math.cos(decl) * math.cos(ha)
    zenith = math.acos(max(-1.0, min(1.0, cos_zen)))

    denom = math.cos(lat) * math.sin(zenith)
    if abs(denom) < 1e-12:
        azimuth = 180.0 if latitude > math.degrees(decl) else 0.0  # sun at zenith/nadir or observer at a pole
    else:
        cos_az = (math.sin(lat) * math.cos(zenith) - math.sin(decl)) / denom
        az = math.degrees(math.acos(max(-1.0, min(1.0, cos_az))))
        azimuth = (az + 180.0) % 360.0 if hour_angle > 0 else (540.0 - az) % 360.0

    elevation = 90.0 - math.degrees(zenith)
    return SolarPosition(
        elevation=elevation,
        apparent_elevation=elevation + _refraction_deg(elevation),
        azimuth=azimuth,
        declination=math.degrees(decl),
        equation_of_time_min=eot,
    )
