# CloudScope Overnight Experiment — Final Report

**Date**: 2026-09-21  
**Duration**: Single overnight run  
**Objective**: Determine technical feasibility of real-time cloud analysis on Arducam B0268

---

## 1. Hardware Used
- **OS**: Windows 11
- **CPU**: Not detected (laptop)
- **RAM**: 16.9 GB
- **GPU**: NVIDIA GeForce RTX 4050 Laptop GPU (6.4 GB VRAM)
- **Disk**: 361 GB free / 1023 GB total
- **CUDA Driver**: 12.7
- **CUDA (PyTorch)**: 11.8
- **PyTorch**: 2.2.2+cu118
- **Python**: 3.11.9

## 2. Dataset
- **Name**: CCSN (Cirrus Cumulus Stratus Nimbus)
- **Source**: Harvard Dataverse (doi:10.7910/DVN/CADDPD), GitHub mirror
- **Total Images**: 2,543
- **Classes**: 11 (WMO genera + contrail)
- **Resolution**: Mixed 400×400 (majority) and 256×256 (3 classes)
- **Format**: JPEG
- **License**: Public (Harvard Dataverse)

### Class Distribution
| Class | Code | Count | % |
|-------|------|-------|---|
| Cirrus | Ci | 139 | 5.5% |
| Cirrostratus | Cs | 287 | 11.3% |
| Cirrocumulus | Cc | 268 | 10.5% |
| Altocumulus | Ac | 221 | 8.7% |
| Altostratus | As | 188 | 7.4% |
| Cumulus | Cu | 182 | 7.2% |
| Cumulonimbus | Cb | 242 | 9.5% |
| Nimbostratus | Ns | 274 | 10.8% |
| Stratocumulus | Sc | 340 | 13.4% |
| Stratus | St | 202 | 7.9% |
| Contrail | Ct | 200 | 7.9% |

## 3. Preprocessing
- **Train/Val/Test Split**: 70/15/15 stratified by class (1774 / 379 / 390)
- **Resize**: All images → 224×224 (center crop from 256)
- **Normalization**: ImageNet mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]
- **Augmentation (train only)**: RandomResizedCrop, RandomHorizontalFlip, ColorJitter
- **No augmentation** on validation/test

## 4. Model Architecture
- **Backbone**: EfficientNet-B0 (ImageNet pretrained)
- **Head**: Linear classifier (1280 → 11 classes)
- **Parameters**: ~5.3M total, ~0.1M trainable (head only initially)
- **Framework**: PyTorch 2.2.2 + torchvision

## 5. Training Configuration
- **Optimizer**: AdamW (lr=1e-3, weight_decay=1e-4)
- **Scheduler**: ReduceLROnPlateau (factor=0.5, patience=3, mode=max)
- **Batch Size**: 32 (fits in 6GB VRAM, ~0.1GB used)
- **Epochs**: 20 (early stopping patience=5)
- **Seed**: 42 (reproducible)
- **Device**: CUDA (GPU 0)

## 6. Training Duration
- **Total Time**: ~5.5 minutes (20 epochs × ~16s/epoch)
- **Best Epoch**: 17 (early stopping would have triggered at epoch 22)
- **GPU Utilization**: Low (~38% in nvidia-smi, batch size limited by small dataset)

## 7. Validation Results (Best Model - Epoch 17)
- **Validation Accuracy**: 55.15%
- **Validation Loss**: 1.6145

## 8. Test Results (Held-out 390 images)
| Metric | Value |
|--------|-------|
| Test Loss | 1.8505 |
| Top-1 Accuracy | 54.62% |
| Top-3 Accuracy | 83.33% |
| Macro Precision | 53.38% |
| Macro Recall | 53.33% |
| Macro F1 | 53.07% |
| Weighted Precision | 54.59% |
| Weighted Recall | 54.62% |
| Weighted F1 | 54.30% |

### Per-Class Performance
| Class | Precision | Recall | F1 | Support |
|-------|-----------|--------|-----|---------|
| Ac | 40.0% | 52.9% | 45.6% | 34 |
| As | 40.0% | 34.5% | 37.0% | 29 |
| Cb | 71.9% | 62.2% | 66.7% | 37 |
| Cc | 62.5% | 61.0% | 61.7% | 41 |
| Ci | 40.0% | 36.4% | 38.1% | 22 |
| Cs | 48.5% | 36.4% | 41.6% | 44 |
| Ct | 84.4% | 90.0% | 87.1% | 30 |
| Cu | 53.6% | 53.6% | 53.6% | 28 |
| Ns | 54.9% | 66.7% | 60.2% | 42 |
| Sc | 64.8% | 67.3% | 66.0% | 52 |
| St | 26.7% | 25.8% | 26.2% | 31 |

### Confusion Matrix (rows=true, cols=pred)
```
      Ac  As  Cb  Cc  Ci  Cs  Ct  Cu  Ns  Sc  St
Ac   18   3   2   3   0   1   1   4   2   0   0
As    3  10   3   1   0   4   0   0   5   1   2
Cb    0   1  23   0   0   0   0   6   2   4   1
Cc    9   0   0  25   3   1   2   1   0   0   0
Ci    4   1   0   2   8   5   2   0   0   0   0
Cs    5   4   0   6   5  16   0   1   3   2   2
Ct    0   0   0   1   0   1  27   0   0   1   0
Cu    0   3   3   0   2   1   0  15   0   0   4
Ns    1   2   1   0   0   1   0   1  28   4   4
Sc    0   0   0   0   1   2   0   0   5  35   9
St    5   1   0   2   1   1   0   0   6   7   8
```

## 9. External Domain Test
**Status**: Limited — no true external dataset available during run

Used 11 images from held-out test set as proxy:
- Contrail (Ct): 97.8% ✓
- Cirrocumulus (Cc): 95.0% ✓
- Altostratus (As): 95.9% ✓
- Cumulonimbus (Cb): 84.4% ✓
- Cirrus (Ci): 76.5% ✓
- **Failures**: Stratus→Altostratus (61%), Cumulus→Stratus (36%), Nimbostratus→Stratocumulus (51%), Cirrostratus→Altostratus (61%)

**Conclusion**: Significant domain shift expected for B0268; fine-tuning required.

## 10. Model File Size
- **Best checkpoint**: ~21 MB (includes optimizer state)
- **Model weights only**: ~20 MB
- **Inference memory**: ~0.1 GB GPU

## 11. Inference Latency
| Scenario | Time |
|----------|------|
| Cold start (model load + 1st forward) | ~260 ms |
| Warm inference (batch, GPU resident) | ~10 ms |
| Single image (script overhead) | ~260 ms |

**Real-time capable**: Yes (>30 FPS warm, ~4 FPS cold start)

## 12. GPU Memory Usage
- **Training**: ~0.1 GB allocated (batch=32, 224×224)
- **Inference**: ~0.1 GB
- **Headroom**: >6 GB available — could increase batch size or resolution

## 13. Segmentation Feasibility (LenghuSky-8 / DINOv3)
- **Backbone**: facebook/dinov3-vitl16-pretrain-lvd1689m ✅ Available
- **Inference Code**: ✅ Complete (`inference_segmentation_dinov3/inference.py`)
- **Labeled Data**: 1,111 images (252 benchmark) ✅ Available
- **Pre-trained Linear Probe**: ❌ **Not publicly distributed**
- **Pre-computed Logits**: 40 GB on Hugging Face (not practical)
- **Action Required**: Train linear probe on 1,111 labeled images (~1-2 hrs)

## 14. Cloud-Base Height Estimation Feasibility
- **Paired Image+Height Dataset**: ❌ None found publicly
- **Candidate Sources**: ARM, SURFRAD, CloudNet — but not paired
- **Requires**: Co-located camera + ceilometer deployment (3-6 months)
- **Recommendation**: Defer; use class-based height priors for now

## 15. B0268 Compatibility
- **Hardware Available**: ❌ No
- **Images Available**: ❌ No
- **Inference Script**: ✅ Ready (`scripts/infer.py`)
- **Domain Gap**: Expected (different optics, sensor, resolution)
- **Next Step**: Collect 50-200 B0268 images, fine-tune

## 16. Failure Analysis
| Issue | Severity | Cause | Mitigation |
|-------|----------|-------|------------|
| Low overall accuracy (55%) | High | Class imbalance, small dataset, similar classes | Data augmentation, class weighting, more data |
| Stratus/Stratocumulus confusion | High | Visual similarity, low texture | Add texture features, attention |
| Cirrus/Cirrostratus confusion | Medium | Thin clouds, similar appearance | Multi-scale features |
| No external validation | Medium | No external dataset downloaded | Priority for next run |

## 17. Recommended Next Experiment
**Priority 1**: Collect B0268 images (50-200) + fine-tune on target domain  
**Priority 2**: Train DINOv3 linear probe for segmentation (1,111 LenghuSky labels)  
**Priority 3**: Expand classification dataset (DeepSky 7k, TJNU 19k) with class mapping  
**Priority 4**: Test class-weighted loss / focal loss for imbalance  
**Priority 5**: Deploy camera + ceilometer for height data collection

---

## 18. Final Feasibility Verdict

### 🟡 YELLOW — Promising but Requires Domain Adaptation / Data / Engineering

**Evidence Supporting Feasibility**:
✅ GPU training works (RTX 4050 6GB sufficient)  
✅ Transfer learning achieves 55% on 11-class problem  
✅ Top-3 accuracy 83% — useful for assisted annotation  
✅ Inference latency <10ms warm — real-time capable  
✅ Model size 20MB — deployable on edge  
✅ Segmentation pipeline code ready (needs probe training)  
✅ Codebase structured for extension

**Key Blockers**:
❌ 55% accuracy insufficient for autonomous operation  
❌ Severe class confusion (St/Sc, Ci/Cs, As/Cs)  
❌ No B0268 domain validation — gap unknown but expected large  
❌ No cloud-height ground truth — altitude regression blocked  
❌ Segmentation probe not available — must train

**Path to Green**:
1. Fine-tune on B0268 data (target domain adaptation)
2. Expand training data (DeepSky + TJNU with class mapping → 10k+ images)
3. Train segmentation probe (1-2 hrs compute)
4. Deploy co-located camera+ceilometer for height data

**Estimated Effort to Prototype**: 2-4 weeks engineering + data collection