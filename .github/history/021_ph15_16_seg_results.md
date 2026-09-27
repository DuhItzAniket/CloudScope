# Ph15-16: Segmentation Training + Eval — MIXED (honest)

## Training
- v1 (`segtools/train_seg.py`): best mIoU **0.6537** ep19 (sky .796 / cloud .793 / contam .371).
- v2 (`segtools/train_seg_v2.py`, stronger color aug sat.6/hue.15): best mIoU
  **0.7105** ep42 (sky .829 / cloud .843 / contam .459). **v2 ships.**
- Setup: 888 train / 223 val chronological, 256px, CE ignore255 weights [1,1,4],
  AdamW 1e-3, patience 12. ~13-15s/epoch on RTX 4050.
- Bugfixes en route: tuple-unpack, decoder scales for non-pow2, cuda→cpu numpy,
  size-matched upsampling (arbitrary input sizes now work).

## Validated ✅
- LenghuSky-distributed val: mIoU 0.71; bright/dark split both ~0.63-0.66
  (no brightness shortcut).
- `results/seg_qa_sheet.jpg`: overlays align on fisheye day+night frames.

##FAILED ❌ (documented, 5 negative tests)
- On CCSN/B0268 daytime frames the model predicts ~85-99% cloud, confidently
  (mean cloud-conf 0.82). Ruled OUT: saturation shortcut (desat worsens),
  scale/framing (all crops fail), brightness split, blue-sky veto (0 flips —
  sky is pale, not saturated), fisheye context (0.955→0.831 only).
- Root cause undetermined (suspect: color-science/exposure gap — high-altitude
  hazy fisheye vs consumer-sensor vivid day; needs paired day data to resolve).

## Consequence for the app (no fabricated boxes)
- Overlays render from measured mask components ONLY when sane:
  gate = largest component <80% frame AND ≥1 valid component,
  else label-only + "mask uncertain" badge. A degenerate mask can NEVER
  produce a fullscreen box.
- `segtools/sky_veto.py` kept as documented heuristic (no-op on current data,
  may help saturated-blue cameras).
- Next data win: a few hand-verified B0268 day masks would unblock this fully.
