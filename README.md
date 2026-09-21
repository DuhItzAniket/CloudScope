# CloudScope

Autonomous overnight ML feasibility experiment for ground-based cloud analysis targeting Arducam B0268 deployment.

## Quick Start

```bash
# Create environment
python -m venv .venv
.venv\Scripts\activate  # Windows
pip install -r requirements.txt

# Run inference on an image
python scripts/infer.py --image path/to/cloud.jpg

# Train from scratch
python scripts/train.py

# Evaluate on test set
python scripts/evaluate.py
```

## Project Structure

```
cloudscope/
├── .github/history/          # Commit history documentation
├── data/
│   ├── raw/ccsn/             # CCSN dataset (2,543 images, 11 classes)
│   ├── processed/ccsn_split/ # Train/Val/Test splits
│   └── external_test/        # External test images
├── models/                   # Trained checkpoints (gitignored)
├── scripts/
│   ├── train.py              # Basic training pipeline
│   ├── train_advanced.py     # Advanced training (MixUp, CutMix)
│   ├── train_multiple_models.py # Multi-architecture training
│   ├── evaluate.py           # Test set evaluation
│   ├── evaluate_all_models.py # Comprehensive evaluation
│   ├── evaluate_tta.py       # Test-time augmentation
│   ├── infer.py              # Single image inference CLI
│   ├── create_split.py       # Data splitting
│   └── visualize_dataset.py  # Dataset visualization
├── results/
│   ├── FINAL_REPORT.md       # Complete experiment report
│   ├── metrics_*.json        # Machine-readable metrics
│   └── *_report.md           # Human-readable reports
├── logs/
│   ├── environment_report.txt
│   └── training_*_log.json   # Per-epoch training history
├── lenghusky8/               # LenghuSky-8 repo (gitignored)
├── PROJECT_STATE.md          # Experiment checkpoint log
├── EXPERIMENT_LOG.md         # Detailed experiment log
└── README.md
```

## Latest Results (Extended Training)

| Model | Val Acc | Test Acc (Top-1) | Test Acc (Top-3) | Macro F1 | Inference |
|-------|---------|------------------|------------------|----------|-----------|
| **MobileNetV3-Large** | **53.56%** | **58.72%** | **82.31%** | **0.5552** | ~13ms |
| EfficientNet-B0 (original) | 55.15% | 54.62% | 83.33% | 0.5307 | ~10ms |
| MobileNetV3-Small | 51.72% | 51.79% | 81.79% | 0.4877 | ~12ms |
| ResNet18 | 48.55% | 49.49% | 80.77% | 0.4718 | ~14ms |

## Key Findings

- **MobileNetV3-Large** is the best performer (58.72% test accuracy)
- Lightweight (~5.4M params) - ideal for edge deployment on B0268
- Simple augmentation outperforms MixUp/CutMix on this dataset
- Top-3 accuracy 82%+ enables assisted annotation workflows
- Stratus (St) and Altocumulus (Ac) are hardest classes (~25% F1)
- Contrail (Ct) and Cumulonimbus (Cb) are easiest (~87% F1)

## Final Verdict

**🟡 YELLOW → 🟢 GREEN trending** - MobileNetV3-Large at 58.72% is approaching usable territory for assisted annotation. With B0268 fine-tuning and class balancing, could reach 65%+.

## Next Steps

1. Collect B0268 images (50-200) for domain adaptation
2. Address class imbalance with weighted loss / oversampling
3. Train ensemble of top 3 models
4. Try 256×256 input resolution for more detail
5. Deploy camera + ceilometer for cloud-height data

## Reproducibility

- Seed: 42 (all splits, training)
- Config: See `scripts/train_multiple_models.py`
- Logs: `logs/training_*_log.json` (per-epoch metrics)
- Checkpoints: `models/cloudscope_*_best.pth`
- Commit history: `.github/history/`