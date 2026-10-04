"""
Self-supervised pretraining (SimCLR) on CCSN dataset, then fine-tune for classification.
This uses the unlabeled data (or treats labels as unknown) to learn good representations.
"""
import os
import json
import time
import random
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from torch.optim.lr_scheduler import CosineAnnealingLR
from tqdm import tqdm

# ===== CONFIG =====
CONFIG = {
    'data_root': r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split',
    'img_size': 224,
    'batch_size': 64,  # Larger for pretraining
    'pretrain_epochs': 100,
    'finetune_epochs': 100,
    'lr_pretrain': 3e-4,
    'lr_finetune': 1e-3,
    'weight_decay': 1e-4,
    'temperature': 0.5,
    'projection_dim': 128,
    'seed': 42,
    'num_workers': 0,
    'device': 'cuda' if torch.cuda.is_available() else 'cpu',
    'checkpoint_dir': r'C:\Users\Luikz\Downloads\CloudScope\models',
    'log_file': r'C:\Users\Luikz\Downloads\CloudScope\logs\self_supervised_log.json',
}

def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed(seed)
        torch.cuda.manual_seed_all(seed)

class SimCLRTransform:
    """SimCLR augmentation: two different views of same image"""
    def __init__(self, img_size=224):
        self.transform = transforms.Compose([
            transforms.RandomResizedCrop(img_size, scale=(0.2, 1.0)),
            transforms.RandomHorizontalFlip(),
            transforms.RandomApply([
                transforms.ColorJitter(0.8, 0.8, 0.8, 0.2)
            ], p=0.8),
            transforms.RandomGrayscale(p=0.2),
            transforms.RandomApply([transforms.GaussianBlur(kernel_size=23)], p=0.5),
            transforms.ToTensor(),
            transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
        ])
    
    def __call__(self, x):
        return self.transform(x), self.transform(x)

class SimCLRModel(nn.Module):
    def __init__(self, base_model='resnet18', projection_dim=128):
        super().__init__()
        if base_model == 'resnet18':
            self.encoder = models.resnet18(weights=None)
            self.encoder.fc = nn.Identity()
            encoder_dim = 512
        elif base_model == 'resnet50':
            self.encoder = models.resnet50(weights=None)
            self.encoder.fc = nn.Identity()
            encoder_dim = 2048
        elif base_model == 'efficientnet_b0':
            self.encoder = models.efficientnet_b0(weights=None)
            self.encoder.classifier = nn.Identity()
            encoder_dim = 1280
        else:
            raise ValueError(f'Unknown base_model: {base_model}')
        
        self.projector = nn.Sequential(
            nn.Linear(encoder_dim, encoder_dim),
            nn.ReLU(),
            nn.Linear(encoder_dim, projection_dim)
        )
    
    def forward(self, x):
        h = self.encoder(x)
        z = self.projector(h)
        return h, z

def ntxent_loss(z1, z2, temperature=0.5):
    """NT-Xent loss for SimCLR"""
    batch_size = z1.shape[0]
    z = torch.cat([z1, z2], dim=0)  # 2B x D
    z = F.normalize(z, dim=1)
    
    # Similarity matrix
    sim = torch.mm(z, z.t()) / temperature  # 2B x 2B
    
    # Mask out self-similarity
    mask = torch.eye(2 * batch_size, device=z.device).bool()
    sim.masked_fill_(mask, -9e15)
    
    # Positive pairs: (i, i+B) and (i+B, i)
    labels = torch.arange(batch_size, device=z.device)
    labels = torch.cat([labels + batch_size, labels])
    
    loss = F.cross_entropy(sim, labels)
    return loss

def pretrain_simclr():
    print("=== SimCLR Pretraining ===")
    set_seed(CONFIG['seed'])
    device = torch.device(CONFIG['device'])
    
    # Data
    transform = SimCLRTransform(CONFIG['img_size'])
    train_dataset = datasets.ImageFolder(
        os.path.join(CONFIG['data_root'], 'train'),
        transform=transform
    )
    train_loader = DataLoader(
        train_dataset, batch_size=CONFIG['batch_size'], shuffle=True,
        num_workers=CONFIG['num_workers'], pin_memory=True, drop_last=True
    )
    
    # Model
    model = SimCLRModel('resnet18', CONFIG['projection_dim']).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=CONFIG['lr_pretrain'], weight_decay=CONFIG['weight_decay'])
    scheduler = CosineAnnealingLR(optimizer, T_max=CONFIG['pretrain_epochs'], eta_min=1e-6)
    
    os.makedirs(CONFIG['checkpoint_dir'], exist_ok=True)
    
    history = []
    best_loss = float('inf')
    
    for epoch in range(CONFIG['pretrain_epochs']):
        model.train()
        epoch_loss = 0
        start = time.time()
        
        for (x1, x2), _ in tqdm(train_loader, desc=f'Epoch {epoch+1}/{CONFIG["pretrain_epochs"]}'):
            x1, x2 = x1.to(device), x2.to(device)
            
            optimizer.zero_grad()
            _, z1 = model(x1)
            _, z2 = model(x2)
            loss = ntxent_loss(z1, z2, CONFIG['temperature'])
            loss.backward()
            optimizer.step()
            
            epoch_loss += loss.item()
        
        epoch_loss /= len(train_loader)
        scheduler.step()
        
        epoch_time = time.time() - start
        lr = optimizer.param_groups[0]['lr']
        
        history.append({
            'epoch': epoch + 1,
            'loss': round(epoch_loss, 4),
            'lr': lr,
            'time_sec': round(epoch_time, 1)
        })
        
        print(f'Epoch {epoch+1}/{CONFIG["pretrain_epochs"]}: loss={epoch_loss:.4f}, lr={lr:.2e}, time={epoch_time:.1f}s')
        
        if epoch_loss < best_loss:
            best_loss = epoch_loss
            torch.save({
                'epoch': epoch + 1,
                'encoder_state_dict': model.encoder.state_dict(),
                'projector_state_dict': model.projector.state_dict(),
                'optimizer_state_dict': optimizer.state_dict(),
                'config': CONFIG,
            }, os.path.join(CONFIG['checkpoint_dir'], 'simclr_pretrain_best.pth'))
    
    # Save final encoder for fine-tuning
    torch.save({
        'encoder_state_dict': model.encoder.state_dict(),
        'config': CONFIG,
    }, os.path.join(CONFIG['checkpoint_dir'], 'simclr_encoder_for_finetune.pth'))
    
    with open(CONFIG['log_file'], 'w') as f:
        json.dump(history, f, indent=2)
    
    print(f'Pretraining complete. Best loss: {best_loss:.4f}')
    return model.encoder

if __name__ == '__main__':
    pretrain_simclr()