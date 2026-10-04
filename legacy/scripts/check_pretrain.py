import torch
import os

path = r'C:\Users\Luikz\Downloads\CloudScope\models\simclr_encoder_for_finetune.pth'
if os.path.exists(path):
    ckpt = torch.load(path, map_location='cpu')
    print(f'Encoder saved: {list(ckpt.keys())}')
    print(f'Config pretrain_epochs: {ckpt["config"]["pretrain_epochs"]}')
else:
    print('Not found')

path2 = r'C:\Users\Luikz\Downloads\CloudScope\models\simclr_pretrain_best.pth'
if os.path.exists(path2):
    ckpt2 = torch.load(path2, map_location='cpu')
    print(f'Best pretrain epoch: {ckpt2["epoch"]}')