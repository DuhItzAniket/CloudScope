import os
import json
import time
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from torch.optim.lr_scheduler import ReduceLROnPlateau

# ===== CONFIG =====
CONFIG = {
    'data_root': r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split',
    'model_name': 'efficientnet_b0',
    'num_classes': 11,
    'img_size': 224,
    'batch_size': 32,  # Adjusted for 6GB VRAM
    'lr': 1e-3,
    'weight_decay': 1e-4,
    'epochs': 20,
    'patience': 5,
    'seed': 42,
    'num_workers': 0,
    'device': 'cuda' if torch.cuda.is_available() else 'cpu',
    'checkpoint_dir': r'C:\Users\Luikz\Downloads\CloudScope\models',
    'log_file': r'C:\Users\Luikz\Downloads\CloudScope\logs\training_log.json',
}

torch.manual_seed(CONFIG['seed'])
if torch.cuda.is_available():
    torch.cuda.manual_seed(CONFIG['seed'])

# ===== DATA =====
train_transform = transforms.Compose([
    transforms.RandomResizedCrop(CONFIG['img_size']),
    transforms.RandomHorizontalFlip(),
    transforms.ColorJitter(brightness=0.2, contrast=0.2, saturation=0.2, hue=0.1),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

val_transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(CONFIG['img_size']),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

train_dataset = datasets.ImageFolder(os.path.join(CONFIG['data_root'], 'train'), transform=train_transform)
val_dataset = datasets.ImageFolder(os.path.join(CONFIG['data_root'], 'val'), transform=val_transform)

train_loader = DataLoader(train_dataset, batch_size=CONFIG['batch_size'], shuffle=True, num_workers=CONFIG['num_workers'], pin_memory=True)
val_loader = DataLoader(val_dataset, batch_size=CONFIG['batch_size'], shuffle=False, num_workers=CONFIG['num_workers'], pin_memory=True)

print(f'Train: {len(train_dataset)}, Val: {len(val_dataset)}')
print(f'Classes: {train_dataset.classes}')
print(f'Device: {CONFIG["device"]}')
if torch.cuda.is_available():
    print(f'GPU: {torch.cuda.get_device_name(0)}, VRAM: {torch.cuda.get_device_properties(0).total_memory/1e9:.1f} GB')

# ===== MODEL =====
model = models.efficientnet_b0(weights=models.EfficientNet_B0_Weights.IMAGENET1K_V1)
model.classifier[1] = nn.Linear(model.classifier[1].in_features, CONFIG['num_classes'])
model = model.to(CONFIG['device'])

# ===== LOSS, OPTIMIZER, SCHEDULER =====
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.AdamW(model.parameters(), lr=CONFIG['lr'], weight_decay=CONFIG['weight_decay'])
scheduler = ReduceLROnPlateau(optimizer, mode='max', factor=0.5, patience=3, verbose=True)

# ===== TRAINING =====
os.makedirs(CONFIG['checkpoint_dir'], exist_ok=True)
os.makedirs(os.path.dirname(CONFIG['log_file']), exist_ok=True)

best_val_acc = 0
best_epoch = 0
epochs_no_improve = 0
history = []

print('\n=== TRAINING START ===')
for epoch in range(CONFIG['epochs']):
    epoch_start = time.time()
    
    # Train
    model.train()
    train_loss = 0
    correct = 0
    total = 0
    
    for images, labels in train_loader:
        images, labels = images.to(CONFIG['device']), labels.to(CONFIG['device'])
        
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
            images, labels = images.to(CONFIG['device']), labels.to(CONFIG['device'])
            outputs = model(images)
            loss = criterion(outputs, labels)
            val_loss += loss.item()
            _, predicted = outputs.max(1)
            total += labels.size(0)
            correct += predicted.eq(labels).sum().item()
    
    val_loss /= len(val_loader)
    val_acc = 100. * correct / total
    
    # Learning rate step
    scheduler.step(val_acc)
    current_lr = optimizer.param_groups[0]['lr']
    
    epoch_time = time.time() - epoch_start
    
    # GPU memory
    gpu_mem = torch.cuda.memory_allocated() / 1e9 if torch.cuda.is_available() else 0
    
    # Log
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
            'classes': train_dataset.classes,
            'config': CONFIG,
        }, os.path.join(CONFIG['checkpoint_dir'], 'cloudscope_classifier_best.pth'))
        print(f'  -> New best model saved (val_acc={val_acc:.2f}%)')
    else:
        epochs_no_improve += 1
    
    # Save last
    torch.save({
        'epoch': epoch + 1,
        'model_state_dict': model.state_dict(),
        'optimizer_state_dict': optimizer.state_dict(),
        'val_acc': val_acc,
        'classes': train_dataset.classes,
        'config': CONFIG,
    }, os.path.join(CONFIG['checkpoint_dir'], 'cloudscope_classifier_last.pth'))
    
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
print(f'Best model: {CONFIG["checkpoint_dir"]}/cloudscope_classifier_best.pth')