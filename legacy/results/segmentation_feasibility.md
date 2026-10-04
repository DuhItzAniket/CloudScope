# Segmentation Feasibility Assessment

## Status: Blocked - Pre-trained Probe Not Available

**The LenghuSky-8 DINOv3 segmentation pipeline requires a trained linear probe (.pt file) which is not publicly distributed.**

## What's Available

| Resource | Status | Notes |
|----------|--------|-------|
| DINOv3 Backbone (facebook/dinov3-vitl16-pretrain-lvd1689m) | ✅ Available | From Hugging Face Transformers |
| Inference Code | ✅ Available | `inference_segmentation_dinov3/inference.py` |
| Labeled Dataset (1,111 images) | ✅ Available | In `baseline_and_benchmark/data/` |
| Pre-computed Logits (~40GB) | ✅ Available | On Hugging Face dataset |
| **Trained Linear Probe** | ❌ **Not Available** | Must train from scratch |

## Requirements to Run Segmentation

1. **Train Linear Probe**: Requires labeled data (1,111 images with sky/cloud/contamination masks)
2. **Training Data**: Available in repo (`baseline_and_benchmark/data/` - 252 manually labeled images for benchmarking)
3. **Compute**: DINOv3 ViT-L/16 backbone + linear probe training on 1,111 images
4. **Time**: Estimated 1-2 hours on RTX 4050 for probe training

## Alternative: Use Pre-computed Logits

The Hugging Face dataset contains pre-computed logits for all 429,620 images:
- Can be used directly for cloud mask extraction
- No training required
- But: 40GB download, specific to LenghuSky camera

## Feasibility Conclusion

**Segmentation is technically feasible** but requires:
- Training a linear probe on the 1,111 labeled images (doable overnight)
- OR downloading 40GB of pre-computed logits (not practical for this run)

## Recommended Next Step

1. Extract the 252 benchmark labeled images from `baseline_and_benchmark/data/`
2. Train a linear probe on DINOv3 local features (same as paper: ViT-L/16, 1024×1024 input)
3. Save probe as `.pt` file
4. Run inference on CloudScope test images

## Code Readiness

The inference script (`inference_segmentation_dinov3/inference.py`) is complete and ready to use once a probe is available. It supports:
- Single image or directory inference
- Logits output (NPZ + per-class heatmaps)
- Visualization overlays (argmax mask, colorized, blended overlay)
- Configurable backbone parameters

---
*Generated: 2026-09-21*