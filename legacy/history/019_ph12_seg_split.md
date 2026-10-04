# Ph12: Seg Split — DONE

- Chronological 80/20 by filename timestamp: train 888 (2018-04 → ~2024),
  val 223 (~2024 → 2025-08-31). Manifest: `data/seg/split.json` (gitignored w/ data).
- Rationale: future-generalization test; adjacent-frame leakage eliminated by
  contiguous blocks (unlike random split).
