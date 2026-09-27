# B0268 Fine-Tuning Session — Overnight Run (6h)

## Session Info
- **Date**: 2026-09-27/28 (overnight)
- **GPU**: NVIDIA RTX 4050 Laptop 6.4GB, CUDA 11.8, PyTorch 2.2.2
- **Starting point**: MobileNetV3-Large best (epoch 29, val 53.56%, test 58.72% Top-1 / 82.31% Top-3)
- **New data**: 25 B0268 images, 4656x3496 RGB, taken 15:44-15:49 IST same day/location
- **Mode**: Autonomous, commit after each phase

---

## Phase 1: Data Preparation + Labeling — COMPLETE

### Rename
- Originals: `WIN_20260927_15_44_21_Pro.jpg` ... `WIN_20260927_15_49_16_Pro.jpg`
- Renamed in place to `b0268_001.jpg` ... `b0268_025.jpg` (chronological = alphabetical)
- Map preserved: `B0268/rename_map.json`
- Method: two-step rename via `__tmp_XX` to avoid collisions

### Visual Inspection
- Read 3 full-res samples (001, 013, 025) + 5x5 contact sheet of all 25
- Contact sheet: `B0268/contact_sheet.jpg` (thumbs 480x360 with model-pred overlay)
- Observation: ALL 25 show same regime — daytime convective puffy white/grey clouds,
  sharp outlines, cauliflower tops, blue sky background. Classic **Cumulus (Cu)**.
  Some frames show larger sheet-like masses (Sc-like, e.g. 017/018) but Cu dominates.
- Obstructions present (domain gap vs CCSN tight crops): sun glare/overexposure,
  vertical lens flare, rooftops, water tanks, poles, buildings, haze.

### Model Predictions (MobileNetV3-Large, CCSN-trained, no adaptation)
- `B0268/model_preds_mobilenetv3large.json`
- Distribution: Cu 9, Cc 5, Ns 5, As 2, Cs 1, Ac 1, Ci 1, Sc 1
- Confidences mostly 0.2-0.7 (low-moderate) — classic domain-shift scatter,
  NOT true class diversity. Example: 001 (visual Cu) predicted Cc 0.32.
- Conclusion: model does not transfer zero-shot to B0268 framing.

### Labels (honest, pending user verification)
- `B0268/b0268_labels.json`: all 25 = `Cu`
- Rationale: single 5-min window, same location/conditions, visual Cu in every frame.
- **Limitations (do not overclaim)**:
  - Labels are agent visual estimates, NOT meteorologist-verified.
  - Effectively ONE independent class sample for 11-way diversity.
  - 25 frames are near-duplicates temporally — leakage risk if split naively.
  - A few frames have Sc-like sheets; user may re-label 017/018 as Sc.
- User will provide more images later covering other classes/conditions.

### Consequence for Strategy
- NO naive 11-way supervised fine-tune on 25 same-class images
  (would bias model to Cu / catastrophic forgetting).
- Correct approach: domain adaptation with CCSN replay (mixed batches),
  low LR, head-focused, early stopping, CCSN regression check.
- Use B0268-25 as: (a) domain-style reference, (b) Cu-calibration set,
  (c) held-out domain probe via cross-val or leave-several-out.

### Files Added (this phase)
- `B0268/b0268_001.jpg` ... `b0268_025.jpg` (renamed originals, ~25MB)
- `B0268/rename_map.json`
- `B0268/b0268_labels.json`
- `B0268/model_preds_mobilenetv3large.json`
- `B0268/contact_sheet.jpg`

---

## Phase 2: Data Integration + Augmentation — PENDING
- Combined loader (CCSN + B0268), B0268-aware aug (glare, haze, horizon bars, hi-res crops)
- Mixed-batch sampling, domain probe split

## Phase 3: Fine-Tuning Strategy Research — PENDING
- Few-shot / domain adaptation literature (web), freeze schedules, LR, regularization

## Phase 4: Fine-Tuning Experiments — PENDING
- Exp A: head-only; Exp B: gradual unfreeze; Exp C: full low-LR (if justified)

## Phase 5: Evaluation — PENDING
- CCSN test regression + B0268 probe metrics + visuals

## Phase 6: Improvement Loop — PENDING

## Phase 7: Production Readiness — PENDING

## Phase 8: Deep Research + Final Report — PENDING
