# Ph6: YOLO26 Viability Verdict — DEFERRED (honest)

## What YOLO26 needs (per Ultralytics docs, researched 2026-09-28)
- `data.yaml` + per-image `.txt` box labels (class + x_center y_center w h, normalized)
- Pretrained COCO weights transfer backbone/neck; detection head re-inits for new nc
- Small-data recipe: freeze=10, AdamW lr0=0.001, epochs=50, patience=20, mosaic≤0.5

## What we have
- CCSN: whole-image class labels only — **zero boxes**
- LenghuSky: polygons (masks), not boxes
- B0268: 25 single-class frames, no boxes
- COCO-pretrained YOLO knows no cloud classes → zero-shot cloud boxes = hallucination

## Verdict
Training/claiming a YOLO26 cloud detector overnight would require **fabricating
box labels** — refused. Path forward (honest):
1. Ship segmentation mask → connected components → bbox/polygon/fill (this session).
2. Those derived boxes become **pseudo-labels** (marked as such) to bootstrap a
   future YOLO26 training set once diverse B0268 images arrive.
3. Revisit YOLO26 when ≥500 diverse box-labeled frames exist.

YOLO26 stays in the architecture as a **future detection head**, behind a feature flag.
