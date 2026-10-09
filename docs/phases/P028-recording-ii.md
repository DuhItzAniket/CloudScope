# P028 — Recording II

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Video and sequence products: SER files with a UTC timestamp per frame, time-lapse assembly from stored pictures,
keograms and star trails.

## Requirements covered
FR-REC-03 (SER; MP4/H.264 "when FFmpeg is available" — not built, see risks), FR-REC-09 (time-lapse assembly),
FR-REC-11 (keograms, star trails).

## Design notes
`core/include/cloudscope/capture/video_files.hpp`:
- `SerWriter`: 178-byte header (`LUCAM-RECORDER`, colour id MONO/RGB/BGR, size, bit depth, frame count,
  observer/instrument/telescope, start time), frames as the camera delivers them (8-bit mono, 16-bit mono, 8-bit
  BGR), then the version-3 trailer with one 64-bit timestamp per frame (.NET ticks, 100 ns since 0001-01-01, UTC).
  The header is rewritten on `close()` with the final frame count; `close()` is safe to call twice and runs from
  the destructor.
- `SerReader`: header, any frame by index, its UTC time from the trailer (the header's start time if a file has no
  trailer); RGB files are returned as BGR; 16-bit data is byte-swapped when the file's order differs from the
  host's.
- **Endianness field (plan D5):** the SER specification says `LittleEndian = 1` means little-endian data, but
  FireCapture wrote 0 for its little-endian files and the readers followed it: Siril's `ser.h` defines
  `SER_LITTLE_ENDIAN = 0, SER_BIG_ENDIAN = 1` with the comment that "later programs, like Siril and GoQat, have
  also decided to implement this header in opposite meaning to the specification" (checked in Siril's source,
  `src/io/ser.h` and `src/io/ser.c`, 2026-10-09). CloudScope writes **0** for its little-endian 16-bit data and
  reads the field the same way. 8-bit files are unaffected either way.
- `assemble_time_lapse(folder, ser, instrument)`: the pictures of a folder (JPEG/PNG/TIFF, name order) into one
  8-bit BGR SER, grey and 16-bit pictures converted, odd sizes resized to the first picture's; the frame times are
  the files' modification times (the sidecar's time would be better; P030's catalogue knows it).
- `keogram(frames, column)`: one column of every frame side by side in time order (the centre column by default);
  `star_trails(frames)`: the per-pixel maximum. Both work on frames in memory; `for_each_picture()` streams a
  folder through them without loading everything.
- MP4/H.264 is not built: FFmpeg is not among the project's dependencies (ADR-008 keeps the dependency list to
  what Debian ships and vcpkg builds quickly), and SER plus the catalogue of pictures covers the data use. A
  time-lapse for people (MP4) can be produced from the SER with ffmpeg on the command line; a built-in encoder is a
  "should" item for a later stage.

## Work log
1. Module and tests (`tests/unit/test_video_files.cpp`).
2. Endianness decision verified against Siril's source and switched to the de-facto convention.

## Verification
- SER round trip (8-bit BGR, 5 frames): header fields, frame bytes identical, every trailer timestamp equal to
  the time given (33 ms apart), file size = 178 + frames + 8 × count; frame 5 of 5 refused.
- SER 16-bit mono: round trip exact; the written field at offset 22 is 0; the same bytes rewritten with the field
  set to 1 and every word swapped read back identical (the reader honours the field).
- A mismatched frame is refused and the writer stays usable; `close()` twice is fine; ticks of the Unix epoch are
  621,355,968,000,000,000.
- Time-lapse from a folder of 7 pictures (6 BGR + 1 grey of another size; a text file ignored): 7 frames in name
  order, the first six identical to the pictures, the seventh resized and grey-to-BGR; an empty folder is
  `NotFound`.
- Keogram of 32 frames with a bar moving one column per frame: column i of the keogram holds frame i's centre
  column (bright only where the bar passes); star trails of the same frames are 200 everywhere; mixed sizes or
  types are refused.
- Opening a CloudScope SER in SER Player or Siril was **not** done (neither is installed here). Owner item: open a
  16-bit SER from `cloudscope-camtool` in SER Player and confirm the frames are not byte-swapped.

## Exit criteria
- [~] SER opens in SER Player and Siril: header layout and endianness convention taken from Siril's source and
  round-trip tested; the viewer check is pending (owner).
- [x] Keogram matches the sequence (test).

## Risks / notes
- The SER header's "local time" field is written with the same UTC value as the UTC field; the trailer carries UTC.
- Frame rates above what the disk sustains are not a SER writer problem at 1080p (6 MB/s for MJPEG-decoded BGR at
  30 fps is 180 MB/s — too much for a laptop SSD over long runs); SER recording is meant for short clips (planets,
  Moon, ISS passes); long runs are pictures through the sequencer.

## Next phase
P029 — Capture sequencer.
