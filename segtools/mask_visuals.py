"""Mask -> detection visuals reference: bbox, polygon, symmetry-fill.

Loads models/cloudscope_seg_best.pth, predicts 3-class mask, extracts cloud
connected components, renders the three overlay modes the desktop app will offer.
Usage: python segtools/mask_visuals.py   (writes results/seg_demo_sheet.jpg)
"""
import os
import torch, torch.nn as nn
import numpy as np
import cv2
from PIL import Image
from torchvision import transforms

from train_seg import UNet

CLS = ['sky', 'cloud', 'contamination']
COLOR = (230, 159, 0)  # cloud orange (BGR for cv2: use reversed below)
DEV = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
TF = transforms.Compose([
    transforms.Resize(512), transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])])


def load_model(path='models/cloudscope_seg_best.pth'):
    ck = torch.load(path, map_location=DEV)
    m = UNet(n=3).to(DEV)
    m.load_state_dict(ck['sd'])
    return m.eval(), ck


def predict_mask(m, img_path, size=512):
    img = Image.open(img_path).convert('RGB')
    w0, h0 = img.size
    x = TF(img).unsqueeze(0).to(DEV)
    with torch.no_grad():
        mask = m(x).argmax(1)[0].cpu().numpy().astype(np.uint8)
    mask = cv2.resize(mask, (w0, h0), interpolation=cv2.INTER_NEAREST)
    return np.array(img), mask


def cloud_objects(mask, min_frac=0.002):
    """Cloud components -> list of {bbox, poly, area}. Honest: from measured mask."""
    cloud = ((mask == 1).astype(np.uint8)) * 255
    n, lab, stats, _ = cv2.connectedComponentsWithStats(cloud, 8)
    objs = []
    h, w = mask.shape
    for i in range(1, n):
        area = int(stats[i, cv2.CC_STAT_AREA])
        if area < min_frac * w * h:
            continue
        x, y, ww, hh = (int(stats[i, k]) for k in
                        (cv2.CC_STAT_LEFT, cv2.CC_STAT_TOP,
                         cv2.CC_STAT_WIDTH, cv2.CC_STAT_HEIGHT))
        comp = ((lab == i).astype(np.uint8)) * 255
        cnts, _ = cv2.findContours(comp, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not cnts:
            continue
        cnt = max(cnts, key=cv2.contourArea)
        poly = cv2.approxPolyDP(cnt, 0.005 * cv2.arcLength(cnt, True), True)
        objs.append({'bbox': (x, y, ww, hh),
                     'poly': poly.reshape(-1, 2).tolist(), 'area': area})
    return objs


def render(img_bgr, objs, label, mode='rect'):
    out = img_bgr.copy()
    col = (0, 159, 230)  # orange in BGR
    for o in objs:
        x, y, w, h = o['bbox']
        pts = np.array(o['poly'], dtype=np.int32)
        if mode == 'rect':
            cv2.rectangle(out, (x, y), (x + w, y + h), col, 3)
        elif mode == 'polygon':
            cv2.polylines(out, [pts], True, col, 3)
        elif mode == 'symmetry':
            cv2.fillPoly(out, [pts], (60, 120, 220))
            cv2.polylines(out, [pts], True, (255, 255, 255), 2)
        cv2.putText(out, label, (x, max(0, y - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
    return out


def main():
    m, ck = load_model()
    print('seg ckpt ep %d miou %.4f' % (ck['ep'], ck['miou']))
    import json
    sp = json.load(open('data/seg/split.json'))
    val_img = 'data/seg/images/%s.jpg' % sp['val'][0]
    b26 = 'B0268/b0268_005.jpg'
    rows = []
    for path in (val_img, b26):
        img_rgb, mask = predict_mask(m, path)
        img_bgr = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2BGR)
        objs = cloud_objects(mask)
        print(path, 'cloud_objs=%d' % len(objs),
              'cloud_frac=%.3f' % (mask == 1).mean())
        small = cv2.resize(img_bgr, (512, 384))
        row = [cv2.resize(img_bgr, (512, 384))]
        for mode in ('rect', 'polygon', 'symmetry'):
            r = render(img_bgr, objs, 'Cu', mode=mode)
            row.append(cv2.resize(r, (512, 384)))
        rows.append(np.hstack(row))
    sheet = np.vstack(rows)
    cv2.imwrite('results/seg_demo_sheet.jpg', sheet,
                [int(cv2.IMWRITE_JPEG_QUALITY), 88])
    print('saved results/seg_demo_sheet.jpg')


if __name__ == '__main__':
    main()
