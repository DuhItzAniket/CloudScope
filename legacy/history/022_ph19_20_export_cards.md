# Ph19-20: ONNX Exports + Model Cards — DONE

## Ph19: Exports + parity
| Model | PyTorch ckpt (local) | ONNX (committed) | Size | Parity |
|-------|---------------------|------------------|------|--------|
| Classifier (MNV3-L, A3) | `models/cloudscope_b0268_expa3_best.pth` | `models/cloudscope_b0268_expa3.onnx` | 16.9MB | max abs diff **4.9e-6**, identical top-3 (Cu .999 on b0268_005) |
| Segmenter (U-Net v2) | `models/cloudscope_seg_v2_best.pth` | `models/cloudscope_seg_v2.onnx` | 57.5MB | argmax agreement **1.0000** |

Dynamic batch (+ dynamic HW for seg). Python ORT here is CPU-only build;
C++ app will use ORT GPU package (CUDA EP) in Ph23.

## Ph20: Model cards
**Classifier**: in 224x224 RGB NCHW float32, ImageNet norm. Out 11 logits
[Ac As Cb Cc Ci Cs Ct Cu Ns Sc St]. Val 55.67 / test 58.21 / top-3 83.08.
Limits: thin-cloud confusion (St/Ac), needs B0268-diverse re-tune later.
**Segmenter**: in 512x512 (any HW, dynamic) RGB NCHW float32. Out 3ch logits
[sky cloud contamination]. Val mIoU 0.71 / cloud IoU 0.84 (LenghuSky-dist).
Limits: FAILS bright daytime consumer-sensor frames (all-cloud); app gates
boxes on component sanity (Ph16 doc). Contamination class weak (0.46 IoU).
