# P027 — Recording I

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Save single pictures as PNG, 16-bit TIFF, JPEG and FITS with the agreed keyword set, each with a JSON sidecar that
says everything known about the frame, written so that a reader never sees a half-written file.

## Requirements covered
FR-REC-01 (JPEG/PNG/TIFF-16), FR-REC-02 (FITS keywords), FR-REC-04 (sidecar content), FR-REC-05 (atomic writes,
SHA-256), FR-REC-08 (versioned schema, reads the interim logger's sidecars). Not covered: FR-REC-10 (AstroTIFF
header) and FR-REC-12 (WCS when calibrated) — see risks.

## Design notes
`core/include/cloudscope/capture/recording.hpp`:
- `write_picture(image, format, file, record)`: PNG and TIFF through OpenCV's encoder, JPEG through OpenCV
  (quality 1–100), FITS through cfitsio. 8-bit data written to TIFF-16 or FITS is scaled by 257, so that the file's
  range is the full 16 bits and no reader has to guess (`BUNIT = 'ADU'` says so). Every file is written as
  `<name>.part<ext>` and renamed into place; the result carries size and SHA-256 (`QCryptographicHash`).
- `write_jpeg_bytes(frame bytes, file)`: an MJPEG frame stored as the camera sent it, no re-encoding (the lossless
  and fastest path; the sequencer uses it when a JPEG is wanted from an MJPEG camera).
- FITS: 16-bit unsigned (`BITPIX 16` with `BZERO`), colour as three planes (`NAXIS3 = 3`, R-G-B), rows written
  bottom-up with `ROWORDER = 'BOTTOM-UP'`, so Siril, ASTAP and DS9 show the picture upright. Keywords: `DATE-OBS`
  (FITS 4.0 form, no offset suffix), `TIMESYS = 'UTC'`, `MJD-OBS`, `TIMESRC`, `EXPTIME` [s], `GAIN`, `OBSGEO-B/L/H`,
  `SITELAT`, `SITELONG`, `SITEELEV`, `SITEID`, `CENTALT`, `CENTAZ`, `POINTSRC`, `SUNALT`, `SUNAZ`, `INSTRUME`,
  `DEVICEID`, `SIMULATE` (T for synthetic data, NFR-DATA-03), `CALIB` (calibration id), `SESSION`, `SWCREATE`,
  `CREATOR`, `BUNIT`. cfitsio works on a **memory file** (`fits_create_memfile` / `fits_open_memfile`) and
  CloudScope moves the bytes to and from disk: cfitsio names files with narrow C strings and parses them for its
  extended syntax, which fails on non-ASCII folder names on Windows (the test workspace has one) and on brackets.
- Sidecar `<picture>.json`, schema `cloudscope.frame/1` (`core/resources/sidecar.schema.json`, compiled by
  `JsonSchema`, `additionalProperties: false` throughout): `file` (name, format, bytes, sha256, size, depth),
  `capture` (UTC `+00:00` form, Unix ms, MJD, monotonic ns, sequence, time source, simulated), `camera` (id, name,
  mode, controls as read back, exposure_ms, gain, calibration_id), optional `site`, `pointing` (with its source),
  `sun`, `statistics` (P024), `session_id`, and `software` (name, version, git revision).
- `read_sidecar(json)` reads both `cloudscope.frame/1` and the interim logger's `cloudscope.sky_logger.frame/1`
  into one `SidecarSummary` (file, hash, time, sequence, site, pointing, Sun, size): the compatibility FR-REC-08
  asks for is that the new software reads the old files. The two layouts differ in field names (`site.latitude`
  vs `site.latitude_deg`, top-level `file`/`sha256`/`bytes` vs a `file` object, `image` vs `file` size); STRATIA's
  ingest (STRATIA P019) reads the old layout and will need the same small mapping for the new one.

## Work log
1. Module, schema resource, tests (`tests/unit/test_recording.cpp`).
2. Found and fixed: cfitsio could not create a file in the test workspace (`espace été`): switched to memory files.
3. Tightened to the SRS wording: atomic writes for every format, `SITEELEV`, `SWCREATE`, the legacy sidecar reader.

## Verification
- FITS round trip: every keyword above read back with its value; `DATE-OBS` = `2026-10-09T10:15:30.123`,
  `MJD-OBS` = 61322.42743; the image read back equals the input ×257 (colour and 16-bit grey); the raw data's first
  stored row is the picture's bottom row and the last stored row holds the top-left white marker (checked with
  cfitsio on the file bytes); the file length is a multiple of 2880.
- PNG round trip exact; TIFF-16 equals the input ×257; JPEG at quality 95 within 4 levels mean absolute error;
  JPEG refuses 16-bit input; no `.part` file remains; writing again replaces the file; SHA-256 of a known text
  ("abc") matches the published digest.
- Sidecar: validates against the schema; removing a required part, adding an unknown key or a malformed hash is
  rejected; a record without site/pointing/Sun/statistics validates; the file written through `.json.part` is the
  document that was given. `read_sidecar` reads a document of each schema and refuses unknown schemas and
  documents without a time.
- Opening the files in Siril and DS9 was **not** done in this phase (neither is installed on the development
  machine); the cfitsio-level checks above cover the keyword set and the row order they rely on. Owner item: open
  one FITS from the first B0268 session in Siril and confirm orientation and `DATE-OBS`.

## Exit criteria
- [x] Sidecar schema valid (tests).
- [~] Files open in Siril/DS9 with correct orientation and keywords: shown with cfitsio; viewer check pending.

## Risks / notes
- **AstroTIFF (FR-REC-10) is not implemented**: OpenCV's TIFF encoder gives no access to the ImageDescription tag.
  Writing it needs libtiff directly (available through OpenCV's vcpkg feature on Windows, `libtiff-dev` on Debian)
  — a small follow-up when a TIFF-reading astronomy tool is actually in the workflow. The sidecar carries the same
  information.
- WCS keywords (FR-REC-12) follow the intrinsic calibration of P031; `CENTALT/CENTAZ` are written now.
- Atomic write means temporary file + rename; the data is not `fsync`ed. A power cut within the OS write-back
  window can lose the last file — acceptable for a sky logger, noted here so nobody assumes more.
- NFR-PERF-02 (16 MP JPEG + sidecar in ≤ 1 s on the laptop) is measured in P032 with the Release build.

## Next phase
P028 — Recording II.
