import os
import json
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from sklearn.metrics import classification_report
import numpy as np

device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
data_root = r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split'
checkpoint_path = r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_simclr_finetuned_best.pth'
img_size = 224
batch_size = 32

test_transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(img_size),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

test_dataset = datasets.ImageFolder(os.path.join(data_root, 'test'), transform=test_transform)
test_loader = DataLoader(test_dataset, batch_size=batch_size, shuffle=False, num_workers=0, pin_memory=True)
classes = test_dataset.classes

checkpoint = torch.load(checkpoint_path, map_location=device)
model = models.resnet18(weights=None)
model.fc = nn.Linear(model.fc.in_features, len(classes))
model.load_state_dict(checkpoint['model_state_dict'])
model = model.to(device)
model.eval()

print(f'Loaded SimCLR fine-tuned ResNet18 from epoch {checkpoint["epoch"]} (val_acc={checkpoint["val_acc"]:.2f}%)')

all_preds = []
all_labels = []
all_probs = []

with torch.no_grad():
    for images, labels in test_loader:
        images, labels = images.to(device), labels.to(device)
        outputs = model(images)
        probs = torch.softmax(outputs, dim=1)
        _, predicted = outputs.max(1)
        all_preds.extend(predicted.cpu().numpy())
        all_labels.extend(labels.cpu().numpy())
        all_probs.extend(probs.cpu().numpy())

all_probs = np.array(all_probs)
total = len(all_labels)
correct = sum(p == l for p, l in zip(all_preds, all_labels))
test_acc = 100. * correct / total

top3_correct = 0
for i, label in enumerate(all_labels):
    top3 = np.argsort(all_probs[i])[-3:]
    if label in top3:
        top3_correct += 1
top3_acc = 100. * top3_correct / total

print(f'Test Accuracy (Top-1): {test_acc:.2f}%')
print(f'Top-3 Accuracy: {top3_acc:.2f}%')
report = classification_report(all_labels, all_preds, target_names=classes, output_dict=True)
print(classification_report(all_labels, all_preds, target_names=classes, digits=4))