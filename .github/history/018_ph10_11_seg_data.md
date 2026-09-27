# Ph10-11: Segmentation Data (rasterize + QA) — DONE

## Ph10: Rasterizer (`segtools/rasterize.py`, stdlib+PIL+numpy)
- Source: `lenghusky8/.../segbaseline/data/*.json` (1111 LabelMe files, embedded
  base64 512x512 images + polygons).
- Typo normalization: contination/contimination→contamination, clode→cloud.
- IDs: sky=0, cloud=1, contamination=2, unlabeled=255.
- Result: **1111/1111 rasterized, 0 skipped** → `data/seg/{images,masks,manifest.json}`.
- Repro: `python segtools/rasterize.py` (~1 min). `data/seg/` gitignored (34.6MB derived).

## Ph11: QA
- Coverage: mean labeled fraction **0.223** (77.7% pixels unlabeled by design —
  conservative annotation); of all pixels: sky 9.4%, cloud 11.2%, contam 1.7%.
- Of LABELED pixels: sky 42%, cloud 50%, contamination 8%.
- Visual: `results/seg_qa_sheet.jpg` — overlays align (blue sky / orange cloud /
  pink contamination) on fisheye day+night frames.
- Training consequence: `ignore_index=255` + class weights for contamination.
