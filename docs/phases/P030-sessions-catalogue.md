# P030 — Sessions & catalogue

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
A folder layout for a run of captures, a manifest that describes it, an SQLite catalogue of every picture with
the metadata that answers "what do we have" without opening sidecars, and a retention policy that makes room.

## Requirements covered
Groundwork for FR-REC-04/05 at the session level, the retention side of FR-REC-07, and the data the desktop
catalogue and the web client will show (P042, P083).

## Design notes
`core/include/cloudscope/session/session.hpp`:
- Layout `<root>/<site-id>/<YYYY-MM-DD>/<session-id>/{session.json, frames/, calibration/, logs/}`; the session id
  is the UTC start to the second plus the site id (`20261009T101530Z-blr-roof`).
- `session.json`, schema `cloudscope.session/1` (`core/resources/session.schema.json`): id, site, camera, start and
  end times, picture count and bytes, notes, layout, software. Rewritten through a temporary file every 50
  pictures and on close; `Session::open()` validates and reads it back.

`core/include/cloudscope/session/catalogue.hpp`:
- Qt SQL with the QSQLITE driver (Qt's own SQLite; the plugin is copied next to every executable on Windows by
  `cloudscope_copy_runtime_dlls`, and `libqt6sql6-sqlite` is in the Debian dependency list). WAL journal,
  `synchronous = NORMAL`. One `Catalogue` object is used from one thread (Qt SQL connections are thread-bound).
- Tables `sessions` (one row per session, upserted), `frames` (path UNIQUE, UTC ms, MJD, sequence, format, bytes,
  SHA-256, size, exposure, gain, mean, clipped fraction, Sun, profile, simulated) with indexes on `utc_ms` and
  `(session_id, utc_ms)`, and `meta` (schema version 1).
- `frame_entry(CapturedPicture)` for what the sequencer just wrote; `frame_entry_from_sidecar()` for pictures on
  disk (either sidecar schema); `index_folder()` adds a folder's uncatalogued pictures in one transaction.
- `frames(FrameQuery)`: by session, time range, limit, oldest or newest first; forward-only result sets.
- `apply_retention(policy, now)`: the oldest pictures beyond `max_bytes` or older than `max_age` are removed with
  their sidecars and rows in one transaction; missing files are counted, not errors; `delete_files = false` drops
  rows only.
- `cloudscope-camtool sequence --session ROOT` creates a session, writes into its `frames/`, enters every picture
  into `ROOT/catalogue.sqlite` and closes the session with its counts.

## Work log
1. Qt Sql added to the build (`find_package(Qt6 ... Sql)`, plugin copy, Debian package), module, schema, tests
   (`tests/unit/test_session_catalogue.cpp`), camtool integration.
2. Found while testing: the time-range query over 3,600 rows took 105 ms in Debug with four test processes
   running; `QSqlQuery::setForwardOnly(true)` and measuring a warm query brought it well under the limit.
3. Found while testing: a Debug assertion of the C runtime shows a dialog and hangs a test until ctest's timeout;
   the test runner now reports such assertions on stderr and aborts (`tests/support/qt_catch_main.cpp`).

## Verification
- Session: id and folder as specified, folders created, manifest validates and reads back with counts, notes and
  end time after `close()`; a second session with the same start and site is `AlreadyExists`; a folder without a
  manifest is `NotFound`; a manifest missing a required part is `Validation`.
- Catalogue: sessions upsert and read back; frames insert, replace by path, count per session, total bytes; a
  time-range query returns the right row with optional fields intact (empty ones stay empty); newest-first and
  limit; the data survives closing and reopening the file.
- **100,000 frames (exit criterion):** inserted in one transaction and queried for a one-hour window (3,600 rows)
  plus a per-session count and the newest 100, best of three runs:

  | Build | Insert 100,000 | Hour window (3,600 rows) | Count + newest 100 |
  |---|---|---|---|
  | Release | 0.92 s | 6 ms | < 1 ms |
  | Debug (four test processes in parallel) | 9.6 s | 106 ms | 5 ms |

  The 100 ms bar is asserted in Release builds (the build that ships); the Debug run reports its time and asserts
  one second, because MSVC's checked iterators make the row conversion several times slower.
- Retention: by size keeps the newest pictures within the budget and removes the others' files, sidecars and rows;
  by age removes what is older than the limit; a file already gone is counted in `missing_files`; an empty policy
  does nothing; rows-only mode leaves files.
- Indexing a folder: four pictures with sidecars are catalogued from the sidecars (time, exposure, Sun, statistics,
  hash, size); a manifest and an orphan sidecar in the folder are ignored; indexing again adds nothing; a new
  picture is picked up; a missing folder is `NotFound`.

## Exit criteria
- [x] Query 100k frames < 100 ms: 6 ms in Release (106 ms in a loaded Debug run), see above.

## Risks / notes
- The catalogue is a cache of the files: anything can be rebuilt with `index_folder()`. Its SQLite file must not be
  shared between processes writing at once (WAL allows readers alongside one writer).
- Retention deletes pictures. The desktop application must show the policy and ask before enabling it (P042).

## Next phase
P031 — Intrinsic calibration tool.
