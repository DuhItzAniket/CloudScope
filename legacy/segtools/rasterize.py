"""LabelMe (LenghuSky) -> segmentation masks. Stdlib + PIL + numpy only.

Reads lenghusky8/baseline_and_benchmark/segbaseline/data/*.json
(each embeds base64 512x512 image + polygon shapes), normalizes label typos,
rasterizes polygons to uint8 masks: 0=sky, 1=cloud, 2=contamination, 255=unlabeled.

Usage:
  python segtools/rasterize.py            # writes data/seg/{images,masks}/ + manifest.json
"""
import os, json, base64, io
import numpy as np
from PIL import Image, ImageDraw

SRC = os.path.join('lenghusky8', 'baseline_and_benchmark', 'segbaseline', 'data')
OUT_IMG = os.path.join('data', 'seg', 'images')
OUT_MSK = os.path.join('data', 'seg', 'masks')
MANIFEST = os.path.join('data', 'seg', 'manifest.json')

FIX = {'contination': 'contamination', 'contimination': 'contamination', 'clode': 'cloud'}
ID = {'sky': 0, 'cloud': 1, 'contamination': 2}


def rasterize(shapes, h, w):
    mask = np.full((h, w), 255, dtype=np.uint8)
    for s in shapes:
        label = FIX.get(s.get('label'), s.get('label'))
        if label not in ID:
            continue
        pts = [(float(x), float(y)) for x, y in s.get('points', [])]
        if len(pts) < 3:
            continue
        layer = Image.new('L', (w, h), 0)
        ImageDraw.Draw(layer).polygon(pts, fill=1)
        mask[np.array(layer, dtype=bool)] = ID[label]
    return mask


def main():
    os.makedirs(OUT_IMG, exist_ok=True)
    os.makedirs(OUT_MSK, exist_ok=True)
    files = sorted(f for f in os.listdir(SRC) if f.endswith('.json'))
    manifest, skipped = [], []
    for f in files:
        stem = f[:-5]
        try:
            d = json.load(open(os.path.join(SRC, f)))
            img = Image.open(io.BytesIO(base64.b64decode(d['imageData']))).convert('RGB')
            w, h = img.size
            mask = rasterize(d.get('shapes', []), h, w)
            img.save(os.path.join(OUT_IMG, stem + '.jpg'), quality=92)
            Image.fromarray(mask).save(os.path.join(OUT_MSK, stem + '.png'))
            manifest.append({'id': stem, 'size': [w, h],
                             'labeled_frac': round(float((mask != 255).mean()), 4)})
        except Exception as e:  # honest skip log, never silent
            skipped.append({'id': stem, 'error': str(e)[:200]})
    json.dump({'masks': manifest, 'skipped': skipped}, open(MANIFEST, 'w'), indent=2)
    print('rasterized %d, skipped %d' % (len(manifest), len(skipped)))


if __name__ == '__main__':
    main()
