import os
import json
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from sklearn.metrics import classification_report, confusion_matrix
import numpy as np

device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
data_root = r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split'
checkpoint_path = r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_mobilenet_v3_large_best.pth'
img_size = 224
batch_size = 16  # Smaller batch for TTA

# Test-time augmentation transforms
tta_transforms = [
    transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.RandomHorizontalFlip(p=1.0),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(280),
        transforms.CenterCrop(img_size),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(280),
        transforms.CenterCrop(img_size),
        transforms.RandomHorizontalFlip(p=1.0),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(256),
        transforms.FiveCrop(img_size),
        transforms.Lambda(lambda crops: torch.stack([
            transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])(transforms.ToTensor()(crop))
            for crop in crops
        ])),
    ]),
]

# Simpler TTA: just use 5 transforms
simple_tta = [
    transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.RandomHorizontalFlip(p=1.0),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(280),
        transforms.CenterCrop(img_size),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(280),
        transforms.CenterCrop(img_size),
        transforms.RandomHorizontalFlip(p=1.0),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
    transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.RandomRotation(10),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
    ]),
]

test_dataset = datasets.ImageFolder(os.path.join(data_root, 'test'), transform=simple_tta[0])
test_loader = DataLoader(test_dataset, batch_size=batch_size, shuffle=False, num_workers=0, pin_memory=True)
classes = test_dataset.classes

checkpoint = torch.load(checkpoint_path, map_location=device)
model = models.mobilenet_v3_large(weights=None)
model.classifier[3] = nn.Linear(model.classifier[3].in_features, len(classes))
model.load_state_dict(checkpoint['model_state_dict'])
model = model.to(device)
model.eval()

print(f'Loaded MobileNetV3-Large from epoch {checkpoint["epoch"]} (val_acc={checkpoint["val_acc"]:.2f}%)')
print(f'Running Test-Time Augmentation with {len(simple_tta)} transforms...')

all_labels = []
all_probs_tta = []

with torch.no_grad():
    for images, labels in test_loader:
        images = images.to(device)
        all_labels.extend(labels.numpy())
        
        batch_probs = []
        for tta_transform in simple_tta:
            # Re-apply transform to original images
            # For simplicity, we'll use the loader images directly for first 2 transforms
            # and apply additional transforms for others
            if tta_transform == simple_tta[0]:
                outputs = model(images)
            elif tta_transform == simple_tta[1]:
                # Horizontal flip
                flipped = torch.flip(images, dims=[3])
                outputs = model(flipped)
            elif tta_transform == simple_tta[2]:
                # Scale up then crop - need to re-process
                pass  # Skip for now, use simpler approach
            else:
                pass  # Skip complex transforms for now
        
        # Simpler: just use original + flip
        outputs1 = model(images)
        flipped = torch.flip(images, dims=[3])
        outputs2 = model(flipped)
        
        # Average probabilities
        probs1 = torch.softmax(outputs1, dim=1)
        probs2 = torch.softmax(outputs2, dim=1)
        avg_probs = (probs1 + probs2) / 2
        batch_probs.append(avg_probs.cpu().numpy())
    
    # This approach is getting complex. Let me do it image by image.

print("TTA evaluation - let me simplify...")

# Actually, let me do a proper per-image TTA
from PIL import Image

def get_tta_predictions(model, image_path, transforms_list, device):
    """Run TTA on a single image"""
    img = Image.open(image_path).convert('RGB')
    all_probs = []
    for t in transforms_list:
        if hasattr(t, 'transforms') and any(isinstance(tr, transforms.FiveCrop) for tr in t.transforms):
            # Handle FiveCrop
            crops = t(img)
            crop_probs = []
            for crop in crops:
                crop = crop.unsqueeze(0).to(device)
                with torch.no_grad():
                    out = model(crop)
                    crop_probs.append(torch.softmax(out, dim=1).cpu().numpy())
            all_probs.append(np.mean(crop_probs, axis=0))
        else:
            inp = t(img).unsqueeze(0).to(device)
            with torch.no_grad():
                out = model(inp)
                all_probs.append(torch.softmax(out, dim=1).cpu().numpy())
    return np.mean(all_probs, axis=0)

# Get all test image paths
test_img_paths = []
test_labels = []
for root, dirs, files in os.walk(os.path.join(data_root, 'test')):
    for f in files:
        if f.lower().endswith(('.jpg', '.jpeg', '.png')):
            cls = os.path.basename(root)
            test_img_paths.append(os.path.join(root, f))
            test_labels.append(classes.index(cls))

print(f'Running TTA on {len(test_img_paths)} images...')

all_preds_tta = []
all_probs_tta = []

for i, (img_path, label) in enumerate(zip(test_img_paths, test_labels)):
    probs = get_tta_predictions(model, img_path, simple_tta, device)
    all_probs_tta.append(probs)
    all_preds_tta.append(np.argmax(probs))
    if (i + 1) % 50 == 0:
        print(f'  Processed {i+1}/{len(test_img_paths)}')

all_probs_tta = np.array(all_probs_tta)
all_preds_tta = np.array(all_preds_tta)
all_labels = np.array(test_labels)

# Accuracy
correct = np.sum(all_preds_tta == all_labels)
test_acc = 100. * correct / len(all_labels)

# Top-3
top3_correct = 0
for i, label in enumerate(all_labels):
    top3 = np.argsort(all_probs_tta[i])[-3:]
    if label in top3:
        top3_correct += 1
top3_acc = 100. * top3_correct / len(all_labels)

print(f'\n=== TTA RESULTS (5 transforms) ===')
print(f'Test Accuracy (Top-1): {test_acc:.2f}%')
print(f'Top-3 Accuracy: {top3_acc:.2f}%')

report = classification_report(all_labels, all_preds_tta, target_names=classes, output_dict=True)
print(classification_report(all_labels, all_preds_tta, target_names=classes, digits=4))

cm = confusion_matrix(all_labels, all_preds_tta)
print('\nConfusion Matrix:')
print(cm)

# Save
results_dir = r'C:\Users\Luikz\Downloads\CloudScope\results'
os.makedirs(results_dir, exist_ok=True)

metrics = {
    'model': 'mobilenet_v3_large_tta',
    'test_acc_top1': test_acc,
    'test_acc_top3': top3_acc,
    'macro_precision': report['macro avg']['precision'],
    'macro_recall': report['macro avg']['recall'],
    'macro_f1': report['macro avg']['f1-score'],
    'weighted_precision': report['weighted avg']['precision'],
    'weighted_recall': report['weighted avg']['recall'],
    'weighted_f1': report['weighted avg']['f1-score'],
    'per_class': {cls: report[cls] for cls in classes},
    'confusion_matrix': cm.tolist(),
    'classes': classes
}

with open(os.path.join(results_dir, 'metrics_mobilenetv3large_tta.json'), 'w') as f:
    json.dump(metrics, f, indent=2)

print('TTA results saved')