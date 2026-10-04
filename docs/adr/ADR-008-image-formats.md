# ADR-008 — Image and video formats and their libraries

Status: Accepted     Date: 2026-10-04     Phase: P008

## Context
Captures must be usable by meteorologists, astronomers (FITS/SER tooling) and STRATIA (JPEG/PNG plus sidecars), with full metadata (FR-REC-01…04).

## Decision
| Format | Use | Library |
|---|---|---|
| JPEG | Default for long sky campaigns (size) | libjpeg-turbo (via OpenCV or directly) |
| PNG, TIFF (8/16-bit) | Lossless stills | OpenCV image codecs |
| **FITS** | Science-grade stills with standard header keywords | cfitsio |
| **SER** | Lossless video sequences (astronomy standard) | In-house writer (simple fixed header + raw frames), validated against the published specification in P028 |
| MP4/H.264 | Shareable time-lapses and recordings | FFmpeg **as an external process** (optional), so its licence and build options never affect CloudScope binaries |
| JSON sidecar | Metadata for every saved frame (versioned schema, compatible with `cloudscope.sky_logger.frame/1`) | nlohmann/json (MIT) |

## Consequences
- Header keyword conventions for FITS and the SER layout are taken from the sources collected in P006 and pinned in P027/P028 with round-trip tests in external tools (e.g. Siril, SER Player).
- No codec is linked that would impose copyleft on CloudScope binaries.
