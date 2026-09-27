# B0268 Overnight Session — Final Report

## Session Summary
- **Window**: 2026-09-27 night → 28 morning (~6h autonomous)
- **GPU**: RTX 4050 6.4GB, all training on-device
- **Starting point**: MobileNetV3-Large (CCSN test 58.72% / top-3 82.31%)
- **New data**: 25 B0268 frames (4656x3496, 5-min window, visual-Cu, sun glare + rooftop obstructions)
- **Commits**: Phase 1 (rename+label) → Phase 2+3 (strategy) → Phase 4-7 (finetune+artifacts) → this report

## Final Results

| Model | CCSN test | Top-3 | Macro F1 | B0268 probe | Source |
|-------|-----------|-------|----------|-------------|--------|
| Baseline (CCSN-only) | 58.72% | 82.31% | 0.5552 | 2/5 | prev session |
| Exp A2 (10x replay) | 55.64% | 82.05% | 0.5333 | 5/5 | this session |
| **Exp A3 (4x replay)** | **58.21%** | **83.08%** | **0.5484** | **5/5** | **FINAL** |
| Ensemble base+A3 | 58.21% | 83.33% | — | 5/5 | inference option |

- **Final model**: Exp A3 — CCSN parity (-0.5pp, noise) + full B0268 Cu calibration + Cu recall 0.39→0.50.
- **Production**: `models/cloudscope_b0268_expa3_best.pth` (17.1MB) + `.onnx` (16.9MB, committed),
  14.4ms GPU / 56ms CPU per image.
- **Inference**: `python scripts/infer.py --image <path> --checkpoint models/cloudscope_b0268_expa3_best.pth`

## Negative Results (documented, not hidden)
1. **Exp A label bug**: single-folder ImageFolder maps Cu→0 (=Ac). Trained B0268→Ac,
   probe 0/5. Caught at phase gate, weights deleted, fixed via `target_transform→7`.
2. **10x replay over-biases Cu** (-3pp CCSN). 4x replay is the sweet spot.
3. **Test-time resolution (288/320 crops) without adaptation**: 54.36%/50.77%,
   below standard 58.21% preprocessing. FixRes-style adaptation needed; deferred.
4. **SimCLR pretraining (prev work)**: 39.74% — too little data for SSL.

## Honest Limitations
- B0268 labels are agent visual estimates (Cu), NOT meteorologist-verified.
- All 25 frames = one 5-min condition → probe measures calibration, not generalization.
- User will supply diverse multi-class B0268 images next session; re-run LP-FT then.
- Deep unfreeze intentionally skipped (wrong lever; risk of backbone Cu-bias).

## Verdict
**🟢 GREEN (conditional)** — B0268 Cu-calibration achieved with zero CCSN regression.
Condition: verify on diverse B0268 conditions when new images arrive.

## Next Session Checklist
1. Ingest new B0268 images → same rename/label/probe pipeline (`B0268/`, `B0268_tr/`, `B0268_pr/`)
2. Re-run LP-FT (Exp A3 recipe: 4x replay, head-only, LR 5e-4) from A3 weights
3. If new classes appear: stratified probe per class, watch Cc/Sc/Ac confusion
4. Consider FixRes fine-tune at 288 if texture-limited classes (Ci/Cc) lag
5. Re-export ONNX + update this report
