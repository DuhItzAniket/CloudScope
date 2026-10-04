import os
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
import time

# Device
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
print(f'Device: {device}')

# Paths
data_root = r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split'

# Transforms
train_transform = transforms.Compose([
    transforms.RandomResizedCrop(224),
    transforms.RandomHorizontalFlip(),
    transforms.ColorJitter(brightness=0.2, contrast=0.2, saturation=0.2, hue=0.1),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

val_transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(224),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

# Datasets (small subset for smoke test)
train_dataset = datasets.ImageFolder(os.path.join(data_root, 'train'), transform=train_transform)
val_dataset = datasets.ImageFolder(os.path.join(data_root, 'val'), transform=val_transform)

# Small subset for smoke test
train_subset = torch.utils.data.Subset(train_dataset, range(min(100, len(train_dataset))))
val_subset = torch.utils.data.Subset(val_dataset, range(min(50, len(val_dataset))))

train_loader = DataLoader(train_subset, batch_size=16, shuffle=True, num_workers=0)
val_loader = DataLoader(val_subset, batch_size=16, shuffle=False, num_workers=0)

print(f'Train samples: {len(train_subset)}, Val samples: {len(val_subset)}')
print(f'Classes: {train_dataset.classes}')

# Model: EfficientNet-B0
model = models.efficientnet_b0(weights=models.EfficientNet_B0_Weights.IMAGENET1K_V1)
num_classes = len(train_dataset.classes)
model.classifier[1] = nn.Linear(model.classifier[1].in_features, num_classes)
model = model.to(device)

# Loss and optimizer
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)

# Smoke test: 1-2 epochs
print('\n--- SMOKE TEST: 2 epochs ---')
for epoch in range(2):
    # Train
    model.train()
    train_loss = 0
    correct = 0
    total = 0
    start = time.time()
    
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
    
    train_acc = 100. * correct / total
    train_time = time.time() - start
    
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
    
    val_acc = 100. * correct / total
    
    print(f'Epoch {epoch+1}: train_loss={train_loss/len(train_loader):.4f}, train_acc={train_acc:.2f}%, val_loss={val_loss/len(val_loader):.4f}, val_acc={val_acc:.2f}%, time={train_time:.1f}s')

# Test checkpoint saving
checkpoint_path = r'C:\Users\Luikz\Downloads\CloudScope\models\smoke_test.pth'
torch.save({
    'epoch': 2,
    'model_state_dict': model.state_dict(),
    'optimizer_state_dict': optimizer.state_dict(),
    'classes': train_dataset.classes,
}, checkpoint_path)
print(f'\nCheckpoint saved to {checkpoint_path}')

# Test loading
checkpoint = torch.load(checkpoint_path, map_location=device)
print(f'Loaded checkpoint: epoch={checkpoint["epoch"]}, classes={checkpoint["classes"]}')

print('\n✓ SMOKE TEST PASSED - Forward/backward pass, checkpoint save/load all working')