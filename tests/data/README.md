# Test fixtures

Small data files that tests read. Everything here is listed in [`manifest.json`](manifest.json) with size, SHA-256, source and licence; the test `fixtures match the manifest` fails if a file is missing, changed or unlisted.

## Rules

1. **Licence-clean only.** A fixture must be CC0/public domain, or made by this project. The manifest names source and licence.
2. **No personal data.** No people, number plates or interiors; no EXIF or GPS metadata (the manifest script refuses files with EXIF).
3. **Small.** At most 100 KB per file and 1 MB in total. Larger test data stays outside the repository.
4. **Real camera recordings** (B0268 sessions) are never committed. Tests that need them, from the camera stage on, read a folder outside the repository.
5. Prefer the synthetic sky generator (`cloudscope/sim/synthetic_sky.hpp`) when a test needs a known ground truth: cloud mask, Sun position, number of saturated pixels.

## Current fixtures

| File | Shows | Source | Licence |
|---|---|---|---|
| `sky/ccsn_ci_n001.jpg` | Bright cirrus on blue sky, some clipped highlights | CCSN database v2, `Ci/Ci-N001.jpg` | CC0 1.0 |
| `sky/ccsn_cu_n001.jpg` | Cumulus against a dark sky, low mean brightness | CCSN database v2, `Cu/Cu-N001.jpg` | CC0 1.0 |
| `sky/ccsn_st_n001.jpg` | Grey, low-contrast layer cloud | CCSN database v2, `St/St-N001.jpg` | CC0 1.0 |

CCSN: Zhang et al., "CloudNet: Ground-based cloud classification with deep convolutional neural network", 2018; Harvard Dataverse, doi:10.7910/DVN/CADDPD, released under CC0 1.0. The three files are 400 × 400 JPEGs, unmodified.

## Adding a fixture

Copy the file into this folder, add an entry to `manifest.json` (SHA-256 with `sha256sum` or `certutil -hashfile <file> SHA256`), add a row above, and run the tests.
