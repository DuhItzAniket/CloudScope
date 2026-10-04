"""Visual QA: top row images, bottom row mask overlays (6 samples)."""
import json
import numpy as np
from PIL import Image

COLORS = np.array([[0, 114, 178], [230, 159, 0], [204, 121, 167]], dtype=np.uint8)
man = json.load(open('data/seg/manifest.json'))['masks']
picks = [man[i]['id'] for i in (0, 150, 400, 650, 900, 1100)]
S = 256
sheet = Image.new('RGB', (S * 6, S * 2))
for i, sid in enumerate(picks):
    img = Image.open('data/seg/images/%s.jpg' % sid).convert('RGB').resize((S, S))
    m = np.array(Image.open('data/seg/masks/%s.png' % sid).resize((S, S), Image.NEAREST))
    ov = np.array(img).copy()
    known = m != 255
    ov[known] = (0.5 * ov[known] + 0.5 * COLORS[m[known]]).astype(np.uint8)
    sheet.paste(img, (i * S, 0))
    sheet.paste(Image.fromarray(ov), (i * S, S))
sheet.save('results/seg_qa_sheet.jpg', quality=88)
print('saved results/seg_qa_sheet.jpg')
