# CloudScope Experiment Log

## Experiment 001: GPU Smoke Test
- **Date**: 2026-09-21
- **Dataset**: CCSN (100 train / 50 val subset)
- **Model**: EfficientNet-B0 (ImageNet pretrained)
- **Image Size**: 224×224
- **Batch Size**: 16
- **Learning Rate**: 1e-3
- **Epochs**: 2
- **Augmentation**: RandomResizedCrop, HorizontalFlip, ColorJitter
- **Seed**: 42
- **GPU**: RTX 4050 Laptop 6.4GB
- **Duration**: ~3s
- **Val Metrics**: Epoch 2 val_acc=66%
- **Status**: PASSED — Forward/backward/checkpoint working

## Experiment 002: Full Training
- **Date**: 2026-09-21
- **Dataset**: CCSN full (1774 train / 379 val / 390 test)
- **Model**: EfficientNet-B0 (ImageNet pretrained)
- **Image Size**: 224×224
- **Batch Size**: 32
- **Learning Rate**: 1e-3 (reduced to 5e-4 at epoch 10)
- **Epochs**: 20 (best at 17)
- **Augmentation**: RandomResizedCrop, HorizontalFlip, ColorJitter
- **Scheduler**: ReduceLROnPlateau (factor=0.5, patience=3)
- **Seed**: 42
- **GPU**: RTX 4050 Laptop 6.4GB
- **Duration**: ~5.5 min (16s/epoch)
- **Best Val Acc**: 55.15% (epoch 17)
- **Best Val Loss**: 1.6145
- **GPU Memory**: 0.1 GB
- **Status**: COMPLETE

## Experiment 003: Test Evaluation
- **Date**: 2026-09-21
- **Model**: Best checkpoint (epoch 17)
- **Test Set**: 390 images (held-out)
- **Top-1 Accuracy**: 54.62%
- **Top-3 Accuracy**: 83.33%
- **Macro F1**: 53.07%
- **Weighted F1**: 54.30%
- **Per-class F1 Range**: 26.2% (St) — 87.1% (Ct)
- **Status**: COMPLETE

## Experiment 004: External Domain Test (Proxy)
- **Date**: 2026-09-21
- **Source**: Test set images (11 samples, 1 per class)
- **Note**: Not true external domain — proxy only
- **Results**: Ct/Cc/As/Cb/Ci correct; St/Cu/Ns/Cs confused
- **Status**: PARTIAL — True external test pending

## Experiment 005: B0268 Inference Script Validation
- **Date**: 2026-09-21
- **Script**: `scripts/infer.py`
- **Cold Start Latency**: ~260 ms
- **Warm Latency**: ~10 ms (estimated from batch)
- **Output Format**: Class, confidence, top-3, latency
- **Status**: COMPLETE — Script ready, hardware validation pending

## Experiment 006: Segmentation Feasibility (LenghuSky-8)
- **Date**: 2026-09-21
- **Method**: DINOv3 ViT-L/16 + Linear Probe
- **Probe Available**: ❌ No
- **Labeled Data**: 1,111 images (252 benchmark)
- **Inference Code**: ✅ Ready
- **Pre-computed Logits**: 40 GB (not practical)
- **Estimated Training**: 1-2 hrs on RTX 4050
- **Status**: BLOCKED — Probe not distributed

## Experiment 007: Cloud-Height Feasibility
- **Date**: 2026-09-21
- **Search**: ARM, SURFRAD, CloudNet, CCSN, LenghuSky, DeepSky, TJNU
- **Paired Image+Height**: None found publicly
- **Recommendation**: Dedicated deployment required
- **Status**: NOT FEASIBLE for supervised baseline