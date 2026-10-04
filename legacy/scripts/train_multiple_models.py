import os
import json
import time
import random
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from torch.optim.lr_scheduler import CosineAnnealingWarmRestarts

# ===== TRAIN A MODEL =====
def train_model(model_name, epochs=80, lr=1e-3, batch_size=32, seed=42):
    # Config
    CONFIG = {
        'data_root': r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split',
        'model_name': model_name,
        'num_classes': 11,
        'img_size': 224,
        'batch_size': batch_size,
        'lr': lr,
        'weight_decay': 1e-4,
        'epochs': epochs,
        'patience': 20,
        'seed': seed,
        'num_workers': 0,
        'device': 'cuda' if torch.cuda.is_available() else 'cpu',
        'checkpoint_dir': r'C:\Users\Luikz\Downloads\CloudScope\models',
        'label_smoothing': 0.05,
    }
    
    # Set seed
    random.seed(CONFIG['seed'])
    np.random.seed(CONFIG['seed'])
    torch.manual_seed(CONFIG['seed'])
    if torch.cuda.is_available():
        torch.cuda.manual_seed(CONFIG['seed'])
    
    device = torch.device(CONFIG['device'])
    
    # Data transforms
    train_transform = transforms.Compose([
        transforms.RandomResizedCrop(CONFIG['img_size'], scale=(0.7, 1.0)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomVerticalFlip(),
        transforms.RandomRotation(15),
        transforms.ColorJitter(brightness=0.3, contrast=0.3, saturation=0.3, hue=0.1),
        transforms.RandomAffine(degrees=0, translate=(0.1, 0.1), scale=(0.9, 1.1)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
        transforms.RandomErasing(p=0.15, scale=(0.02, 0.1), ratio=(0.3, 3.3)),
    ])
    
    val_transform = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(CONFIG['img_size']),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ])
    
    train_dataset = datasets.ImageFolder(
        os.path.join(CONFIG['data_root'], 'train'), 
        transform=train_transform
    )
    val_dataset = datasets.ImageFolder(
        os.path.join(CONFIG['data_root'], 'val'), 
        transform=val_transform
    )
    
    train_loader = DataLoader(
        train_dataset, 
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
    if model_name == 'efficientnet_b0':
        weights = models.EfficientNet_B0_Weights.IMAGENET1K_V1
        model = models.efficientnet_b0(weights=weights)
        model.classifier[1] = nn.Linear(model.classifier[1].in_features, CONFIG['num_classes'])
    elif model_name == 'resnet18':
        weights = models.ResNet18_Weights.IMAGENET1K_V1
        model = models.resnet18(weights=weights)
        model.fc = nn.Linear(model.fc.in_features, CONFIG['num_classes'])
    elif model_name == 'mobilenet_v3_small':
        weights = models.MobileNet_V3_Small_Weights.IMAGENET1K_V1
        model = models.mobilenet_v3_small(weights=weights)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, CONFIG['num_classes'])
    elif model_name == 'mobilenet_v3_large':
        weights = models.MobileNet_V3_Large_Weights.IMAGENET1K_V1
        model = models.mobilenet_v3_large(weights=weights)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, CONFIG['num_classes'])
    else:
        raise ValueError(f'Unknown model: {model_name}')
    
    model = model.to(device)
    
    criterion = nn.CrossEntropyLoss(label_smoothing=CONFIG['label_smoothing'])
    optimizer = torch.optim.AdamW(model.parameters(), lr=CONFIG['lr'], weight_decay=CONFIG['weight_decay'])
    scheduler = CosineAnnealingWarmRestarts(optimizer, T_0=10, T_mult=2, eta_min=1e-6)
    
    os.makedirs(CONFIG['checkpoint_dir'], exist_ok=True)
    
    best_val_acc = 0
    best_epoch = 0
    epochs_no_improve = 0
    history = []
    
    print(f'\n=== TRAINING {model_name.upper()} ===')
    print(f'Epochs: {CONFIG["epochs"]}, Batch: {CONFIG["batch_size"]}, LR: {CONFIG["lr"]}')
    
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
        
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_epoch = epoch + 1
            epochs_no_improve = 0
            torch.save({
                'epoch': epoch + 1,
                'model_state_dict': model.state_dict(),
                'optimizer_state_dict': optimizer.state_dict(),
                'val_acc': val_acc,
                'classes': train_dataset.classes,
                'config': CONFIG,
            }, os.path.join(CONFIG['checkpoint_dir'], f'cloudscope_{model_name}_best.pth'))
            print(f'  -> New best model saved (val_acc={val_acc:.2f}%)')
        else:
            epochs_no_improve += 1
        
        torch.save({
            'epoch': epoch + 1,
            'model_state_dict': model.state_dict(),
            'optimizer_state_dict': optimizer.state_dict(),
            'val_acc': val_acc,
            'classes': train_dataset.classes,
            'config': CONFIG,
        }, os.path.join(CONFIG['checkpoint_dir'], f'cloudscope_{model_name}_last.pth'))
        
        if epochs_no_improve >= CONFIG['patience']:
            print(f'\nEarly stopping at epoch {epoch+1}')
            break
    
    log_file = os.path.join(r'C:\Users\Luikz\Downloads\CloudScope\logs', f'training_{model_name}_log.json')
    with open(log_file, 'w') as f:
        json.dump(history, f, indent=2)
    
    print(f'\n=== {model_name.upper()} COMPLETE ===')
    print(f'Best val acc: {best_val_acc:.2f}% at epoch {best_epoch}')
    return best_val_acc, best_epoch

if __name__ == '__main__':
    # Train multiple architectures
    for model_name in ['resnet18', 'mobilenet_v3_small', 'mobilenet_v3_large']:
        try:
            train_model(model_name, epochs=80)
        except Exception as e:
            print(f'Error training {model_name}: {e}')