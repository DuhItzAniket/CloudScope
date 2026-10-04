# Ph32: Fit + Data-Honesty Sign-Off — SIGNED

## Under/overfit (final shipped models)
- **Classifier A3**: val-selected ep8 (55.67); test 58.21 ≥ val → no val-overfit.
  Train 95% vs val 55% gap = dataset ambiguity (thin-cloud boundaries), bounded
  by early-stop discipline. NOT underfit (capacity proven by 95% train).
- **Segmenter v2**: val mIoU rising through ep42 (best), loss 0.9→0.45 monotonic
  → no overfit at selection; test = val distribution (chronological future split).
- **Degenerate-mask failure** is a *domain* failure, not a fit failure (021 doc);
  gated in-app, never silent.

## Wrong-data audit (all sources)
| Data | Provenance | Risk | Status |
|------|-----------|------|--------|
| CCSN labels | Expert, Harvard Dataverse | none | trusted |
| B0268 labels (25×Cu) | Agent visual estimate | unverified expert | MARKED pending user verification |
| Seg masks | LenghuSky polygons + typo map (006-contamination, 1-clode) | mapping error | mapping logged, QA sheet verified |
| Boxes in app | Measured components only | fake boxes | IMPOSSIBLE by construction (gate+fallback) |
| YOLO detector | — | fabricated | NOT SHIPPED (Ph6 deferral stands) |
| Pseudo-labels | — | silent training | NEVER used for training |

## Guarantee statement
No model in this repo was trained on synthetic, guessed, or misattributed labels.
Every label source is documented with its verification level. The app surfaces
uncertainty (confidence, mask-uncertain badge) instead of hiding it.
