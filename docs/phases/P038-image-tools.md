# P038 — Image tools

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Focus aid, loupe, pixel inspector and region-of-interest statistics (FR-DSP-04, FR-DSP-05).

## Requirements covered
FR-DSP-04, FR-DSP-05.

## Design notes
- `image_tools.hpp`: `focus_measure()` (variance of the Laplacian of the luma in a region), `pixel_value()`
  (B, G, R and BT.601 luma), `roi_statistics()` (count, mean, spread, minimum, maximum per channel, luma mean,
  sharpness), `loupe_image()` (nearest-neighbour magnification with black outside the picture), `clamp_roi()`.
- `ImageToolsPanel`: the focus value with a history sparkline and the best value seen (the operator turns the
  focus ring until the line peaks), the loupe around the cursor, the inspector line and the region statistics.
  The region is selected with Shift-drag on the live view.

## Work log
1. `image_tools.cpp`, `image_tools_panel.cpp`; the live view reports the cursor's image pixel and the region.

## Verification
- `ui.` tests `[image_tools]`: a uniform region has zero spread and zero focus; a sharp edge scores above its
  blurred copy; the inspector reads channels and luma and refuses points off the image; regions are clipped to
  the image; the loupe magnifies without blending and pads with black; a synthetic Sun's disc is saturated in all
  channels.

## Exit criteria
- [x] Unit tests on known images.

## Safety & failure-mode notes
- The focus measure depends on the scene; the panel shows the best value seen so that the operator compares
  against it, not against an absolute scale.

## Deviations & next phase
- None. Next: P039 — Capture panel.
