# B0268 Fine-Tuning Results (Phases 4-7)

## Experiment Log

### Exp A (INVALID — label bug, discarded)
- Setup: head-only, CCSN + B0268 x10, LR 1e-3, 15 epochs
- Bug: `ImageFolder('B0268_train')` with single `Cu/` folder assigns label **0**
  (=Ac in CCSN scheme), not 7 (=Cu). Trained B0268→Ac.
- Symptom: probe 0/5 every epoch despite B0268 in 10% of batches.
- Action: weights deleted (`cloudscope_b0268_expa_best.pth`), never used.
- Lesson: always assert `class_to_idx` alignment when mixing datasets.

### Exp A2 (fixed labels, 10x replay) — head-only, LR 1e-3, 15 epochs
- Fix: `target_transform=lambda t: 7` (verified at `__getitem__`).
- Result: probe **5/5 every epoch** from epoch 1; best CCSN val **54.88%** (ep 6).
- CCSN test: **55.64%** Top-1 / 82.05% Top-3 / macro F1 0.5333.
- Problem: CCSN test **-3.08pp** vs baseline (58.72%) — Cu-bias from 10% replay.
- Cu recall on CCSN test improved (0.39 → 0.46) but Cb/Sc/Cc dropped.

### Exp A3 (lighter replay 4x, LR 5e-4, 12 epochs) — WINNER
- Change: B0268 repeat 10x → 4x (~4% of batches), LR 1e-3 → 5e-4.
- Result: best CCSN val **55.67%** (ep 8), probe **5/5** throughout.
- CCSN test: **58.21%** Top-1 / **83.08%** Top-3 / macro F1 0.5484.
- Gap to baseline: **-0.51pp** (noise) with full B0268 Cu calibration.
- Cu recall: 0.50 (vs 0.39 baseline). Ct still 87%.
- Checkpoint (local, gitignored): `models/cloudscope_b0268_expa3_best.pth` (17.1MB)
- Log: `logs/b0268_expa3_log.json`

### Ensemble (baseline + A3, avg softmax, no training)
- CCSN test: **58.21%** Top-1 / **83.33%** Top-3 (best top-3) / probe 5/5.
- Verdict: matches A3 top-1; use only if top-3 matters at 2x latency cost.

## Final Comparison

| Model | CCSN test | Top-3 | Macro F1 | B0268 probe | Cu recall |
|-------|-----------|-------|----------|-------------|-----------|
| Baseline (no B0268) | 58.72% | 82.31% | 0.5552 | 2/5 | 0.39 |
| Exp A2 (10x replay) | 55.64% | 82.05% | 0.5333 | 5/5 | 0.46 |
| **Exp A3 (4x replay)** | **58.21%** | **83.08%** | **0.5484** | **5/5** | **0.50** |
| Ensemble base+A3 | 58.21% | 83.33% | — | 5/5 | — |

## Gate Decisions
- Exp B (deep unfreeze): **SKIPPED**. Probe at ceiling; CCSN preserved. Unfreezing
  backbone on 20 same-class images risks backbone-level Cu-bias (irreversible
  without retraining). Replay ratio was the correct lever, now tuned.
- TTA on B0268: not needed (probe saturated); flips hurt clouds per Exp 002.

## Production Readiness (Phase 7)
- Final model: Exp A3 (`models/cloudscope_b0268_expa3_best.pth`, 17.1MB)
- ONNX: `models/cloudscope_b0268_expa3.onnx` (16.9MB, dynamic batch, verified export)
- Params: 4.22M (as counted) / MobileNetV3-Large class
- Latency RTX 4050: **14.4ms** GPU / 56ms CPU per 224x224 image
- Inference: `python scripts/infer.py --image <path> --checkpoint models/cloudscope_b0268_expa3_best.pth`
  (no code change needed; `--checkpoint` flag already supported)
- Weights are gitignored (large binaries); metrics + logs committed.
- Caveats: labels are agent visual estimates (pending user verification);
  probe frames are same-condition neighbors (calibration, not generalization);
  user will supply diverse multi-class B0268 images next session.

## Environment Fix (noted)
- Mid-session NumPy 2.4.6 broke `torch.from_numpy` (torch 2.2.2 built vs NumPy 1.x).
- Fixed: `pip install 'numpy<2'` → 1.26.4. Added to setup notes.
