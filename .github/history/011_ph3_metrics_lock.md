# Ph3: Metrics Lock (final, torch 2.5.1 verified)

| Model | Val acc | Test Top-1 | Test Top-3 | Macro F1 | B0268 probe | Status |
|-------|---------|------------|------------|----------|-------------|--------|
| MNV3-Large CCSN-only (base) | 53.56 (ep29) | 58.72% | 82.31% | 0.5552 | 2/5 | reference |
| **MNV3-Large + B0268 A3** | **55.67 (ep8)** | **58.21%** | **83.08%** | **0.5484** | **5/5** | **SHIP** |
| Ensemble base+A3 | — | 58.21% | 83.33% | — | 5/5 | inference option |
| EfficientNet-B0 orig | 55.15 (ep17) | 54.62% | 83.33% | 0.5307 | — | superseded |
| MNV3-Large focal/sampler variants | 52.8/53.3 | 54.9/55.6 | 0.53 | — | — | discarded |

A3 production stats: 4.22M params, ckpt 17.1MB, ONNX 16.9MB,
latency 14.4ms GPU / 56ms CPU @224x224, Cu recall 0.39→0.50.
These numbers are frozen for the desktop-app integration (Ph30 gate).
