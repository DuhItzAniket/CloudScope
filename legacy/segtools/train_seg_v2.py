"""Seg v2: stronger color augmentation vs vivid-blue OOD (CCSN/B0268).

Same U-Net + data + metric as train_seg.py; only augmentation changes:
saturation 0.2->0.6, hue added (0.15), brightness/contrast 0.3->0.4.
Writes models/cloudscope_seg_v2_best.pth + logs/training_seg_v2_log.json.
"""
import os, json, time, random
import numpy as np
import torch, torch.nn as nn, torch.nn.functional as F
from torch.utils.data import DataLoader
from torchvision import transforms
from PIL import Image

from train_seg import UNet, SegDS, CLS

ROOT, CKPT, LOG = 'data/seg', 'models/cloudscope_seg_v2_best.pth', 'logs/training_seg_v2_log.json'
SIZE, BATCH, EPOCHS, PATIENCE, SEED = 256, 16, 60, 12, 42


def main():
    random.seed(SEED)
    np.random.seed(SEED)
    torch.manual_seed(SEED)
    dev = torch.device('cuda')
    print('seg v2 train on', torch.cuda.get_device_name(0))
    tf_img = transforms.Compose([
        transforms.RandomResizedCrop(SIZE, scale=(0.7, 1.0)),
        transforms.RandomHorizontalFlip(),
        transforms.ColorJitter(brightness=0.4, contrast=0.4, saturation=0.6, hue=0.15),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])])
    tf_msk = transforms.Compose([
        transforms.RandomResizedCrop(SIZE, scale=(0.7, 1.0), interpolation=Image.NEAREST),
        transforms.RandomHorizontalFlip()])
    ev_img = transforms.Compose([
        transforms.Resize(SIZE + 32), transforms.CenterCrop(SIZE), transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])])
    ev_msk = transforms.Compose([
        transforms.Resize(SIZE + 32, interpolation=Image.NEAREST),
        transforms.CenterCrop(SIZE)])
    sp = json.load(open('%s/split.json' % ROOT))
    tr = DataLoader(SegDS(sp['train'], tf_img, tf_msk), batch_size=BATCH, shuffle=True,
                    num_workers=0, pin_memory=True, drop_last=True)
    va = DataLoader(SegDS(sp['val'], ev_img, ev_msk), batch_size=BATCH, shuffle=False,
                    num_workers=0, pin_memory=True)
    m = UNet().to(dev)
    crit = nn.CrossEntropyLoss(ignore_index=255,
                               weight=torch.tensor([1.0, 1.0, 4.0]).to(dev))
    opt = torch.optim.AdamW(m.parameters(), lr=1e-3, weight_decay=1e-4)
    best, bad, hist = 0, 0, []
    for ep in range(EPOCHS):
        t0 = time.time()
        m.train()
        tl = 0
        for im, ms in tr:
            im, ms = im.to(dev), ms.to(dev)
            opt.zero_grad()
            l = crit(m(im), ms)
            l.backward()
            opt.step()
            tl += l.item()
        m.eval()
        inter = np.zeros(3)
        union = np.zeros(3)
        with torch.no_grad():
            for im, ms in va:
                im, ms = im.to(dev), ms.to(dev)
                pd = m(im).argmax(1).cpu().numpy()
                t = ms.cpu().numpy()
                for c in range(3):
                    p = (pd == c) & (t != 255)
                    r = (t == c)
                    inter[c] += (p & r).sum()
                    union[c] += (p | r).sum()
        iou = inter / np.maximum(union, 1)
        mi = float(np.nanmean(iou))
        print('v2 ep %d loss=%.4f miou=%.4f sky=%.3f cloud=%.3f contam=%.3f t=%.0fs' % (
            ep + 1, tl / len(tr), mi, iou[0], iou[1], iou[2], time.time() - t0))
        hist.append({'ep': ep + 1, 'loss': round(tl / len(tr), 4), 'miou': round(mi, 4),
                     'iou': [round(float(x), 4) for x in iou]})
        if mi > best:
            best, bad = mi, 0
            torch.save({'ep': ep + 1, 'sd': m.state_dict(), 'miou': mi,
                        'iou': iou.tolist(), 'cls': CLS}, CKPT)
            print('  saved best')
        else:
            bad += 1
        if bad >= PATIENCE:
            print('early stop at', ep + 1)
            break
    json.dump(hist, open(LOG, 'w'), indent=1)
    print('SEG v2 done best_miou=%.4f' % best)


if __name__ == '__main__':
    main()
