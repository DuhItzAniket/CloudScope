# P036 — Histogram, scopes & stretch

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
A live histogram (linear and logarithmic) with clipping indicators, the RGB parade, and a display stretch
(manual and automatic) applied to the preview only, with an over-exposure highlight (FR-DSP-02/03/11).

## Requirements covered
FR-DSP-02, FR-DSP-03, FR-DSP-11.

## Design notes
- `display_stretch.hpp`: pure functions. `make_lut(black, white, gamma)`, `auto_levels(histogram, low, high)`
  (percentiles of the luma histogram), `stretch_lut(settings, histogram)` (identity when disabled),
  `apply_lut()` (a copy), `channel_histograms()`, `rgb_parade()` (per-strip distributions on a reduced copy),
  `highlight_overexposed()` (magenta, a colour the sky never has), `night_vision()`, `clipping_fractions()`.
- The preview pipeline applies the table to a **copy** of the decoded picture; the frames in the hub, the
  statistics, the recorder and the sequencer never see it. The histogram panel states what is in effect
  ("In effect: black 12, white 231, gamma 1.00").
- `HistogramWidget` paints the luma line over the three channel curves with the clipped shares at both ends
  (bars scaled so that 10 % fills the height, and the percentages as text); `ParadeWidget` paints the three
  strips tinted red, green, blue.

## Work log
1. `display_stretch.cpp`, `histogram_panel.cpp`; the pipeline computes the statistics at most every 200 ms and
   the parade only while the panel shows it.

## Verification
- `ui.` tests `[stretch]`: identity table, levels and gamma against the offline formula, automatic levels from a
  known histogram, the stretched image is a copy and the original is unchanged, channel histograms count every
  pixel, the parade's columns sum to one and a constant channel lands in one bin, the highlight paints exactly
  the saturated pixels of a synthetic sky and leaves the source alone, the night rendering is red only.
- Recorded files unaffected by the stretch: by construction (the stretch lives in the preview copy; P039's
  capture test writes pictures while a stretch is enabled in the pipeline? No: the capture path never reads the
  pipeline at all, it reads the hub), and the test above shows the input image is untouched.
- Manual with the B0268: the histogram follows exposure changes; automatic levels brighten a dark scene; the
  magenta highlight marks the Sun's disc and clipped cloud tops.

## Exit criteria
- [x] Matches offline computation; recorded files unaffected by stretch.

## Safety & failure-mode notes
- The highlight colour is reserved for the display; a user cannot mistake it for a recorded value.

## Deviations & next phase
- None. Next: P037 — Overlays.
