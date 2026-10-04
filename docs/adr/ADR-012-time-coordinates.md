# ADR-012 — Time and coordinate conventions shared with STRATIA

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
CloudScope produces data that STRATIA trains on, and STRATIA's model consumes geometry inputs (ray maps, Sun angles) that CloudScope computes. If the two projects disagree on a sign, a zero or a time zone, the model silently learns or predicts nonsense.

## Decision
- **Time:** stored as UTC with explicit offset (ISO 8601, milliseconds); scheduling uses the monotonic clock; every record states its time source (`host`, `ntp`, `gps-pps`). Local time is display-only.
- **Azimuth:** from geographic north, clockwise (east = 90°). **Elevation:** from the horizon, up positive (zenith = 90°). Local frame: East-North-Up.
- **Image:** pixel (u, v) from the top-left pixel centre, u right, v down. Camera frame right-handed with +z along the optical axis, +x towards +u, +y towards +v.
- **Sun:** true and apparent (refracted) elevation stored separately.
- **Units in files and APIs:** metres, seconds, degrees.

These are the conventions already used by the interim sky logger (P003) and are written into STRATIA's interface contract (stratia-contract v1, STRATIA P008).

## Consequences
- A shared test vector file (pixel → ray → az/el for a reference calibration and time) is added to both repositories when STRATIA P043 and CloudScope P031 implement camera models; both must produce identical numbers.
