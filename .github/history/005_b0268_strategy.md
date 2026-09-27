# B0268 Fine-Tuning Strategy (LP-FT + Replay)

## Situation
- Source: CCSN 1774 train / 379 val / 390 test, 11 classes
- Target: 25 B0268 frames, single 5-min window, all visual-Cu, 4656x3496 with
  sun glare, lens flare, rooftops/tanks/poles, haze
- Split: B0268 001-020 train (x10 repeat = 200 eff), 021-025 held-out probe (Cu recall)
- Base: MobileNetV3-Large best (val 53.56%, test 58.72%)
- Leakage note: probe frames are same-condition as train (temporal neighbors);
  probe measures same-condition Cu calibration, NOT generalization. Honest.

## Literature Basis (researched 2026-09-28)
1. **LP-FT** (linear-probe-then-finetune): train head 3-5 epochs frozen backbone,
   then unfreeze last blocks at 1/10 LR for 5-15 epochs. +2-5% few-shot, less
   feature distortion. (Kumar et al.; few-shot torchvision guides)
2. **Freeze for tiny data**: <500 target samples -> freeze backbone first;
   full fine-tune at same LR causes catastrophic forgetting/overfit.
3. **Replay helps target too**: mixing source data during fine-tune improves
   TARGET efficiency up to ~1.9x, not just anti-forgetting. Use mixed batches
   ~85-90% CCSN / 10-15% B0268.
4. **BatchNorm**: keep BN in eval / frozen stats on tiny target batches to avoid
   statistic shift. Freeze BN layers throughout.
5. **Progressive unfreeze**: after head converges, unfreeze last 1-2 blocks only.
6. **Class-aware forgetting**: new-class (Cu) replay risks biasing old similar
   classes (Cc, Sc, Ac); monitor CCSN per-class, esp. Cc/Sc/Ac/Cs.

## Experiments
- **Exp A (head-only)**: freeze features+BN, train classifier, LR 1e-3, 15 epochs,
  mixed batches. Expect: B0268 probe Cu recall up, CCSN val stable.
- **Exp B (gradual)**: unfreeze last feature blocks, LR 1e-4 (1/10), 15 epochs.
  Proceed only if Exp A shows no CCSN regression.
- **Exp C (conditional)**: full low-LR only if B needed and stable.

## B0268-Specific Augmentation
- Base: same strong pipeline as CCSN winners (RRC 0.7-1.0, flips, rot15, jitter,
  affine, erasing) + wider RRC scale (0.5-1.0) to include obstruction variety.
- CCSN frames unchanged; B0268 gets extra sun-glare-tolerant jitter.
- No MixUp/CutMix (hurts small cloud data per Exp 002).

## Metrics / Gates
- B0268 probe: Cu recall (5/5 target), mean confidence on correct.
- CCSN val: overall acc must stay within 2pp of 53.56%; watch Cc/Sc/Ac/Cs.
- CCSN test (final only): compare vs 58.72% baseline.
- Gate: proceed B only if CCSN val drop < 2pp AND probe recall improves.

## Risks
- Single-class target -> Cu bias. Mitigated by replay ratio + low LR + early stop.
- Probe leakage (same window). Documented; user will supply diverse images next.
- Labels are agent visual estimates (pending user verification).
