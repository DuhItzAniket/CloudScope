# P040 — Session browser

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Browse the frame catalogue with thumbnails, metadata and filters, compare two pictures, export a selection
(FR-DAT-02).

## Requirements covered
FR-DAT-02; the UI side of FR-DAT-01.

## Design notes
- `CatalogueModel` (`session_browser.hpp`): a table model over `Catalogue::frames()`, newest first, fetched in
  pages of 500 rows as the view scrolls (`canFetchMore` / `fetchMore`, with the time of the oldest row fetched as
  the cursor), filtered by session, time range (in the query) and by format and simulated data (on the page).
  A catalogue of 100,000 rows therefore costs one query of 500 rows to open.
- Thumbnails are decoded on Qt's global thread pool (`cv::imdecode` at reduced size, FITS through
  `read_fits_image`) and cached; a picture that cannot be read is remembered as such.
- `export_frames()` copies pictures and their sidecars into a folder and writes `export.json`
  (`cloudscope.export/1`) with the catalogue's SHA-256, size, time and session of every picture.
- Compare: two selected rows side by side with their metadata.

## Work log
1. `session_browser.cpp`; the browser reopens the catalogue when a capture finishes or the session root changes.

## Verification
- `ui.a catalogue of 100,000 frames opens at once and is browsed in pages`: opening the browser over 100,000 rows
  took 10 ms (limit 2,000), five further pages 10 ms (limit 1,500); newest first;
  filters by session, measured-only, format and time range return exactly the expected rows.
- `ui.thumbnails are decoded and an export copies pictures, sidecars and a manifest`: 96-pixel thumbnails with
  the aspect kept; the export's manifest carries the SHA-256 and the sidecar name; a missing file is counted, not
  fatal; thumbnails arrive in the background in the browser.

## Exit criteria
- [x] Browses 100k-frame catalogue smoothly (timings above; the view never holds more rows than were scrolled to).

## Safety & failure-mode notes
- The browser never deletes; retention (P030) stays a separate, explicit action.
- The export copies files; the catalogue's checksum lets the receiver verify them.

## Deviations & next phase
- None. Next: P041 — Preferences.
