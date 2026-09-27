# Ph1: PyTorch 2.5 Verification — PASS

- Env: torch **2.5.1+cu121**, torchvision 0.20.1+cu121, CUDA True (12.1),
  RTX 4050 Laptop GPU, numpy 1.26.4.
- Old `transformers` warning ("PyTorch >= 2.4 required") is **gone** under 2.5.
- Repro: Exp A3 checkpoint re-evaluated on CCSN test → **top1=58.21 top3=83.08**,
  bit-identical to the 2.2.2 run. Metrics across sessions remain comparable.
- Forward smoke test on MobileNetV3-Large OK.
