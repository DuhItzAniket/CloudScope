# P006 — Competitive analysis: decisions

Date: 2026-10-04 · Evidence: [`P006_competitive_analysis_sources.md`](P006_competitive_analysis_sources.md) (product profiles, feature matrix, format facts; 98 cited sources, unverified items listed in its §5)

## Products reviewed

| Product | Version (2026-10-04) | Platform | Licence | Family |
|---|---|---|---|---|
| SharpCap | 4.1 | Windows | Closed, Pro £14/year | Desktop capture |
| AMCap | 9.23 (2017) | Windows | Shareware | Basic webcam capture |
| FireCapture | 2.7.15 | Win/macOS/Linux/Pi | Closed, private use only | Planetary capture |
| N.I.N.A. | 3.2 | Windows | MPL-2.0 | Deep-sky automation |
| KStars/Ekos + INDI | 3.8.5 / 2.2.5 | Win/macOS/Linux/Pi | GPL / LGPL-2.1 | Deep-sky automation, device protocol |
| indi-allsky | 2026.09 | Linux/Pi | GPL-3.0 | Headless all-sky |
| AllSky (AllskyTeam) | 2026.10.01 | Raspberry Pi OS | MIT | Headless all-sky |
| ASCOM Alpaca | Device API v1 | Any (REST) | MIT spec | Device standard |

## What we learned

1. **No product combines a desktop workbench with an autonomous all-sky node.** Capture tools are operator-driven desktop apps; all-sky tools are headless Pi services. CloudScope's combination is unoccupied, and KStars/Ekos (C++/Qt, cross-platform) is the nearest technical peer.
2. **Cloud "AI" in existing tools classifies cover state** (clear / cloudy / overcast, with a raw confidence score) or estimates cover heuristically (star count, IR sky temperature, red/blue ratio). **None offers WMO genus classification, cloud-base height, or calibrated uncertainty.**
3. **No product drives a pan-tilt sky survey with a Sun keep-out zone.**
4. **Already common, so not differentiators:** keograms, star trails, time-lapses, day/night auto-exposure by Sun altitude, MQTT/Home Assistant, web dashboards, IR cloud sensing, optical-flow nowcasting, fisheye lens solving.

## Decisions

| # | Decision | Effect |
|---|---|---|
| D1 | Match the table stakes of both families before adding AI | SRS v1.1 adds FR-CAM-12 (raw/ROI/binning), FR-DSP-11 (preview-only stretch, over-exposure highlight), FR-REC-10 (AstroTIFF), FR-REC-11 (keograms, star trails), FR-SEQ-06 (day/night profiles by Sun elevation), FR-REM-09 (MQTT + Home Assistant) |
| D2 | Adopt **ASCOM Alpaca** both ways: client for mounts/focusers/switches/ObservingConditions; server exposing CloudScope as SafetyMonitor + ObservingConditions | FR-CTL-12, FR-REM-10; ADR-011. Lets N.I.N.A. and SharpCap users consume CloudScope's sky assessment directly — a distribution channel for STRATIA's output |
| D3 | **INDI client** on Linux as an optional backend to reuse existing drivers (libcamera, ASI, AAG CloudWatcher) | FR-CTL-13 (could); ADR-011 |
| D4 | **FITS keyword set** follows FITS 4.0 plus the SBFITSEXT/N.I.N.A. conventions: DATE-OBS (UTC), TIMESYS, MJD-OBS, EXPTIME, GAIN, OBSGEO-B/L/H, SITELAT/SITELONG (decimal, east positive), CENTALT/CENTAZ, ROWORDER, SWCREATE; WCS (ZEA/ZPN) when calibrated | FR-REC-02 tightened; FR-REC-12 (could) |
| D5 | **SER v3** with UTC timestamp trailer; set the endianness flag the way Siril / SER Player read it, proven by round-trip tests (the spec and de-facto readers disagree) | P028 exit criterion |
| D6 | **Licence hygiene:** do not link StellarSolver (GPL-3.0); do not reuse code or assets from closed-source products | Consistent with ADR-010 |
| D7 | **Differentiators to build and protect:** WMO genus classification, calibrated cloud-base height with uncertainty, uncertainty-aware safety decisions, pan-tilt sky survey with Sun keep-out, one codebase for desktop and headless node, native C++ on Windows and Pi 5 | Stages E–G and STRATIA integration (Stage F) |

## Not adopted (for now)

- Native vendor SDKs (ZWO, QHY, Player One, ToupTek, SVBony): the target camera is UVC; vendor cameras are reachable later through Alpaca or INDI. Revisit after v1.0.
- Plate solving and live stacking: deep-sky features outside CloudScope's purpose.
