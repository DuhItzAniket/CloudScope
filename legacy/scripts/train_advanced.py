import os
import json
import time
import random
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, Subset
from torchvision import datasets, transforms, models
from torch.optim.lr_scheduler import CosineAnnealingLR, CosineAnnealingWarmRestarts
from sklearn.model_selection import StratifiedKFold
from sklearn.metrics import classification_report, confusion_matrix

# ===== CONFIG =====
CONFIG = {
    'data_root': r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split',
    'model_name': 'efficientnet_b0',
    'num_classes': 11,
    'img_size': 224,
    'batch_size': 32,
    'lr': 1e-3,
    'weight_decay': 1e-4,
    'epochs': 100,
    'patience': 15,
    'seed': 42,
    'num_workers': 0,
    'device': 'cuda' if torch.cuda.is_available() else 'cpu',
    'checkpoint_dir': r'C:\Users\Luikz\Downloads\CloudScope\models',
    'log_file': r'C:\Users\Luikz\Downloads\CloudScope\logs\training_advanced_log.json',
    'use_mixup': True,
    'mixup_alpha': 0.2,
    'use_cutmix': True,
    'cutmix_alpha': 1.0,
    'label_smoothing': 0.1,
    'warmup_epochs': 5,
}

def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed(seed)
        torch.cuda.manual_seed_all(seed)
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False

def mixup_data(x, y, alpha=1.0):
    if alpha > 0:
        lam = np.random.beta(alpha, alpha)
    else:
        lam = 1
    batch_size = x.size()[0]
    index = torch.randperm(batch_size).to(x.device)
    mixed_x = lam * x + (1 - lam) * x[index, :]
    y_a, y_b = y, y[index]
    return mixed_x, y_a, y_b, lam

def mixup_criterion(criterion, pred, y_a, y_b, lam):
    return lam * criterion(pred, y_a) + (1 - lam) * criterion(pred, y_b)

def cutmix_data(x, y, alpha=1.0):
    if alpha > 0:
        lam = np.random.beta(alpha, alpha)
    else:
        lam = 1
    batch_size = x.size()[0]
    index = torch.randperm(batch_size).to(x.device)
    
    W = x.size()[2]
    H = x.size()[3]
    cut_rat = np.sqrt(1. - lam)
    cut_w = int(W * cut_rat)
    cut_h = int(H * cut_rat)
    
    cx = np.random.randint(W)
    cy = np.random.randint(H)
    
    bbx1 = np.clip(cx - cut_w // 2, 0, W)
    bby1 = np.clip(cy - cut_h // 2, 0, H)
    bbx2 = np.clip(cx + cut_w // 2, 0, W)
    bby2 = np.clip(cy + cut_h // 2, 0, H)
    
    x[:, :, bbx1:bbx2, bby1:bby2] = x[index, :, bbx1:bbx2, bby1:bby2]
    lam = 1 - ((bbx2 - bbx1) * (bby2 - bby1) / (W * H))
    
    y_a, y_b = y, y[index]
    return x, y_a, y_b, lam

def get_model(model_name, num_classes, pretrained=True):
    if model_name == 'efficientnet_b0':
        weights = models.EfficientNet_B0_Weights.IMAGENET1K_V1 if pretrained else None
        model = models.efficientnet_b0(weights=weights)
        model.classifier[1] = nn.Linear(model.classifier[1].in_features, num_classes)
    elif model_name == 'resnet18':
        weights = models.ResNet18_Weights.IMAGENET1K_V1 if pretrained else None
        model = models.resnet18(weights=weights)
        model.fc = nn.Linear(model.fc.in_features, num_classes)
    elif model_name == 'mobilenet_v3_small':
        weights = models.MobileNet_V3_Small_Weights.IMAGENET1K_V1 if pretrained else None
        model = models.mobilenet_v3_small(weights=weights)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    elif model_name == 'mobilenet_v3_large':
        weights = models.MobileNet_V3_Large_Weights.IMAGENET1K_V1 if pretrained else None
        model = models.mobilenet_v3_large(weights=weights)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    else:
        raise ValueError(f'Unknown model: {model_name}')
    return model

# ===== MAIN =====
set_seed(CONFIG['seed'])

device = torch.device(CONFIG['device'])
print(f'Device: {device}')
if torch.cuda.is_available():
    print(f'GPU: {torch.cuda.get_device_name(0)}')

# Data transforms with stronger augmentation
train_transform = transforms.Compose([
    transforms.RandomResizedCrop(CONFIG['img_size'], scale=(0.7, 1.0)),
    transforms.RandomHorizontalFlip(),
    transforms.RandomVerticalFlip(),
    transforms.RandomRotation(15),
    transforms.ColorJitter(brightness=0.3, contrast=0.3, saturation=0.3, hue=0.1),
    transforms.RandomAffine(degrees=0, translate=(0.1, 0.1), scale=(0.9, 1.1)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    transforms.RandomErasing(p=0.2, scale=(0.02, 0.1), ratio=(0.3, 3.3)),
])

val_transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(CONFIG['img_size']),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])

# Load full training dataset
full_train_dataset = datasets.ImageFolder(
    os.path.join(CONFIG['data_root'], 'train'), 
    transform=train_transform
)
val_dataset = datasets.ImageFolder(
    os.path.join(CONFIG['data_root'], 'val'), 
    transform=val_transform
)

print(f'Train: {len(full_train_dataset)}, Val: {len(val_dataset)}')
print(f'Classes: {full_train_dataset.classes}')

# Create data loaders
train_loader = DataLoader(
    full_train_dataset, 
    batch_size=CONFIG['batch_size'], 
    shuffle=True, 
    num_workers=CONFIG['num_workers'], 
    pin_memory=True,
    drop_last=True
)
val_loader = DataLoader(
    val_dataset, 
    batch_size=CONFIG['batch_size'], 
    shuffle=False, 
    num_workers=CONFIG['num_workers'], 
    pin_memory=True
)

# Model
model = get_model(CONFIG['model_name'], CONFIG['num_classes']).to(device)

# Loss with label smoothing
criterion = nn.CrossEntropyLoss(label_smoothing=CONFIG['label_smoothing'])

# Optimizer
optimizer = torch.optim.AdamW(
    model.parameters(), 
    lr=CONFIG['lr'], 
    weight_decay=CONFIG['weight_decay']
)

# Scheduler: Cosine annealing with warm restarts
scheduler = CosineAnnealingWarmRestarts(
    optimizer, 
    T_0=10, 
    T_mult=2, 
    eta_min=1e-6
)

# Training
os.makedirs(CONFIG['checkpoint_dir'], exist_ok=True)
os.makedirs(os.path.dirname(CONFIG['log_file']), exist_ok=True)

best_val_acc = 0
best_epoch = 0
epochs_no_improve = 0
history = []

print(f'\n=== ADVANCED TRAINING START: {CONFIG["model_name"]} ===')
print(f'Epochs: {CONFIG["epochs"]}, Batch: {CONFIG["batch_size"]}, LR: {CONFIG["lr"]}')
print(f'Mixup: {CONFIG["use_mixup"]}, CutMix: {CONFIG["use_cutmix"]}, Label Smoothing: {CONFIG["label_smoothing"]}')

for epoch in range(CONFIG['epochs']):
    epoch_start = time.time()
    
    # Train
    model.train()
    train_loss = 0
    correct = 0
    total = 0
    
    for images, labels in train_loader:
        images, labels = images.to(device), labels.to(device)
        
        optimizer.zero_grad()
        
        # Apply MixUp or CutMix
        if CONFIG['use_mixup'] and CONFIG['use_cutmix']:
            if random.random() < 0.5:
                images, y_a, y_b, lam = mixup_data(images, labels, CONFIG['mixup_alpha'])
                outputs = model(images)
                loss = mixup_criterion(criterion, outputs, y_a, y_b, lam)
            else:
                images, y_a, y_b, lam = cutmix_data(images, labels, CONFIG['cutmix_alpha'])
                outputs = model(images)
                loss = mixup_criterion(criterion, outputs, y_a, y_b, lam)
        elif CONFIG['use_mixup']:
            images, y_a, y_b, lam = mixup_data(images, labels, CONFIG['mixup_alpha'])
            outputs = model(images)
            loss = mixup_criterion(criterion, outputs, y_a, y_b, lam)
        elif CONFIG['use_cutmix']:
            images, y_a, y_b, lam = cutmix_data(images, labels, CONFIG['cutmix_alpha'])
            outputs = model(images)
            loss = mixup_criterion(criterion, outputs, y_a, y_b, lam)
        else:
            outputs = model(images)
            loss = criterion(outputs, labels)
        
        loss.backward()
        optimizer.step()
        
        train_loss += loss.item()
        _, predicted = outputs.max(1)
        total += labels.size(0)
        correct += predicted.eq(labels).sum().item()
    
    train_loss /= len(train_loader)
    train_acc = 100. * correct / total
    
    # Validate
    model.eval()
    val_loss = 0
    correct = 0
    total = 0
    with torch.no_grad():
        for images, labels in val_loader:
            images, labels = images.to(device), labels.to(device)
            outputs = model(images)
            loss = criterion(outputs, labels)
            val_loss += loss.item()
            _, predicted = outputs.max(1)
            total += labels.size(0)
            correct += predicted.eq(labels).sum().item()
    
    val_loss /= len(val_loader)
    val_acc = 100. * correct / total
    
    scheduler.step()
    current_lr = optimizer.param_groups[0]['lr']
    
    epoch_time = time.time() - epoch_start
    gpu_mem = torch.cuda.memory_allocated() / 1e9 if torch.cuda.is_available() else 0
    
    log_entry = {
        'epoch': epoch + 1,
        'train_loss': round(train_loss, 4),
        'train_acc': round(train_acc, 2),
        'val_loss': round(val_loss, 4),
        'val_acc': round(val_acc, 2),
        'lr': current_lr,
        'gpu_mem_gb': round(gpu_mem, 2),
        'time_sec': round(epoch_time, 1)
    }
    history.append(log_entry)
    
    print(f'Epoch {epoch+1}/{CONFIG["epochs"]}: train_loss={train_loss:.4f}, train_acc={train_acc:.2f}%, val_loss={val_loss:.4f}, val_acc={val_acc:.2f}%, lr={current_lr:.2e}, gpu_mem={gpu_mem:.2f}GB, time={epoch_time:.1f}s')
    
    # Save best
    if val_acc > best_val_acc:
        best_val_acc = val_acc
        best_epoch = epoch + 1
        epochs_no_improve = 0
        torch.save({
            'epoch': epoch + 1,
            'model_state_dict': model.state_dict(),
            'optimizer_state_dict': optimizer.state_dict(),
            'val_acc': val_acc,
            'classes': full_train_dataset.classes,
            'config': CONFIG,
        }, os.path.join(CONFIG['checkpoint_dir'], f'cloudscope_{CONFIG["model_name"]}_best.pth'))
        print(f'  -> New best model saved (val_acc={val_acc:.2f}%)')
    else:
        epochs_no_improve += 1
    
    # Save last
    torch.save({
        'epoch': epoch + 1,
        'model_state_dict': model.state_dict(),
        'optimizer_state_dict': optimizer.state_dict(),
        'val_acc': val_acc,
        'classes': full_train_dataset.classes,
        'config': CONFIG,
    }, os.path.join(CONFIG['checkpoint_dir'], f'cloudscope_{CONFIG["model_name"]}_last.pth'))
    
    # Early stopping
    if epochs_no_improve >= CONFIG['patience']:
        print(f'\nEarly stopping at epoch {epoch+1} (no improvement for {CONFIG["patience"]} epochs)')
        break

# Save history
with open(CONFIG['log_file'], 'w') as f:
    json.dump(history, f, indent=2)

print(f'\n=== TRAINING COMPLETE ===')
print(f'Best val acc: {best_val_acc:.2f}% at epoch {best_epoch}')
print(f'Total epochs: {len(history)}')
print(f'Best model: {CONFIG["checkpoint_dir"]}/cloudscope_{CONFIG["model_name"]}_best.pth')