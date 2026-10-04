# Ph13-14: Seg Tooling Committed + Training Setup — DONE

## Ph13
Covered by commits 0cf22a2 (rasterizer, QA sheet, coverage) + bac2ce9 (split).

## Ph14: U-Net Setup (`segtools/train_seg.py`)
- Minimal U-Net, ResNet18 ImageNet encoder, 3-class head (sky/cloud/contamination).
- 256x256, batch 16, AdamW 1e-3, 60 epochs, patience 12, seed 42.
- Loss: CrossEntropy `ignore_index=255`, weights [1,1,4] (contamination rare).
- Metric: mean IoU over labeled pixels; best checkpoint
  `models/cloudscope_seg_best.pth` (gitignored).
- Augment: RRC(0.7-1.0) applied jointly to image (bilinear) + mask (nearest),
  hflip, light color jitter (image only).
