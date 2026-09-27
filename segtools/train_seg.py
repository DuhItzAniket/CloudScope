"""Tiny U-Net (ResNet18 encoder, ImageNet) for sky/cloud/contamination.

Data: data/seg/{images,masks} + split.json. ignore_index=255.
Usage: python segtools/train_seg.py  (writes models/cloudscope_seg_best.pth)
"""
import os, json, time, random
import numpy as np
import torch, torch.nn as nn, torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader
from torchvision import transforms, models
from PIL import Image

ROOT = 'data/seg'
CKPT = 'models/cloudscope_seg_best.pth'
LOG = 'logs/training_seg_log.json'
SIZE, BATCH, EPOCHS, PATIENCE, SEED = 256, 16, 60, 12, 42
CLS = ['sky', 'cloud', 'contamination']


class SegDS(Dataset):
    def __init__(self, ids, tf_img, tf_msk):
        self.ids, self.tf_img, self.tf_msk = ids, tf_img, tf_msk

    def __len__(self):
        return len(self.ids)

    def __getitem__(self, i):
        sid = self.ids[i]
        img = Image.open('%s/images/%s.jpg' % (ROOT, sid)).convert('RGB')
        msk = Image.open('%s/masks/%s.png' % (ROOT, sid))
        return self.tf_img(img), torch.from_numpy(np.array(self.tf_msk(msk))).long()


class Block(nn.Module):
    def __init__(self, ci, co):
        super().__init__()
        self.c = nn.Sequential(nn.Conv2d(ci, co, 3, padding=1, bias=False),
                               nn.BatchNorm2d(co), nn.ReLU(True),
                               nn.Conv2d(co, co, 3, padding=1, bias=False),
                               nn.BatchNorm2d(co), nn.ReLU(True))

    def forward(self, x):
        return self.c(x)


class UNet(nn.Module):
    def __init__(self, n=3):
        super().__init__()
        e = models.resnet18(weights=models.ResNet18_Weights.IMAGENET1K_V1)
        self.e0 = nn.Sequential(e.conv1, e.bn1, e.relu, e.maxpool)
        self.e1, self.e2, self.e3, self.e4 = e.layer1, e.layer2, e.layer3, e.layer4
        self.u4 = Block(512 + 256, 256)
        self.u3 = Block(256 + 128, 128)
        self.u2 = Block(128 + 64, 64)
        self.u1 = Block(64 + 64, 64)
        self.out = nn.Conv2d(64, n, 1)

    def forward(self, x):
        x0 = self.e0(x)
        x1, x2, x3, x4 = self.e1(x0), self.e2(x1), self.e3(x2), self.e4(x3)
        d = self.u4(torch.cat([F.interpolate(x4, scale_factor=2, mode='nearest'), x3], 1))
        d = self.u3(torch.cat([F.interpolate(d, scale_factor=2, mode='nearest'), x2], 1))
        d = self.u2(torch.cat([F.interpolate(d, scale_factor=2, mode='nearest'), x1], 1))
        d = self.u1(torch.cat([F.interpolate(d, scale_factor=2, mode='nearest'), x0], 1))
        return self.out(F.interpolate(d, scale_factor=2, mode='bilinear', align_corners=False))


def miou(pred, tgt, n=3):
    ious = []
    for c in range(n):
        p, t = (pred == c), (tgt == c)
        u = (p | t).sum().item()
        ious.append((p & t).sum().item() / u if u else float('nan'))
    return ious


def main():
    random.seed(SEED)
    np.random.seed(SEED)
    torch.manual_seed(SEED)
    dev = torch.device('cuda')
    print('seg train on', torch.cuda.get_device_name(0))
    tf_img = transforms.Compose([
        transforms.RandomResizedCrop(SIZE, scale=(0.7, 1.0)),
        transforms.RandomHorizontalFlip(),
        transforms.ColorJitter(brightness=0.3, contrast=0.3, saturation=0.2),
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
                t = ms.numpy()
                for c in range(3):
                    p = (pd == c) & (t != 255)
                    r = (t == c)
                    inter[c] += (p & r).sum()
                    union[c] += (p | r).sum()
        iou = inter / np.maximum(union, 1)
        mi = float(np.nanmean(iou))
        print('ep %d loss=%.4f miou=%.4f sky=%.3f cloud=%.3f contam=%.3f t=%.0fs' % (
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
    print('SEG done best_miou=%.4f' % best)


if __name__ == '__main__':
    main()
