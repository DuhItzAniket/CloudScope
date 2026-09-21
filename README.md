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
├── data/
│   ├── raw/ccsn/                 # CCSN dataset (2,543 images, 11 classes)
│   ├── processed/ccsn_split/     # Train/Val/Test splits
│   └── external_test/            # External test images
├── models/
│   ├── cloudscope_classifier_best.pth
│   └── cloudscope_classifier_last.pth
├── scripts/
│   ├── train.py                  # Full training pipeline
│   ├── evaluate.py               # Test set evaluation
│   ├── infer.py                  # Single image inference
│   ├── smoke_test.py             # Quick validation
│   ├── create_split.py           # Data splitting
│   ├── visualize_dataset.py      # Dataset visualization
│   └── external_test.py          # Domain shift test
├── results/
│   ├── FINAL_REPORT.md           # Complete experiment report
│   ├── dataset_report.md         # Dataset statistics
│   ├── test_report.md            # Test metrics
│   ├── metrics.json              # Machine-readable metrics
│   ├── dataset_samples.png       # Sample images grid
│   ├── segmentation_feasibility.md
│   ├── altitude_feasibility.md
│   └── b0268_qualitative.md
├── logs/
│   ├── environment_report.txt    # Hardware/software audit
│   └── training_log.json         # Per-epoch training history
├── lenghusky8/                   # LenghuSky-8 repo (for segmentation)
└── PROJECT_STATE.md              # Experiment checkpoint log
```

## Key Results

| Metric | Value |
|--------|-------|
| Test Accuracy (Top-1) | 54.62% |
| Test Accuracy (Top-3) | 83.33% |
| Macro F1 | 53.07% |
| Best Val Accuracy | 55.15% |
| Inference (warm) | ~10 ms |
| Model Size | ~20 MB |
| GPU Memory | 0.1 GB / 6.4 GB |

## Final Verdict

**🟡 YELLOW** — Promising but requires domain adaptation/data/engineering

See `results/FINAL_REPORT.md` for complete analysis.

## Next Steps

1. Collect B0268 images (50-200) and fine-tune
2. Train DINOv3 linear probe for segmentation (1,111 LenghuSky labels)
3. Expand training data with DeepSky/TJNU datasets
4. Deploy camera + ceilometer for cloud-height data collection

## Reproducibility

- Seed: 42 (all splits, training)
- Config: See `scripts/train.py` CONFIG dict
- Logs: `logs/training_log.json` (per-epoch metrics)
- Checkpoints: `models/cloudscope_classifier_best.pth`