import os
import json
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from sklearn.metrics import classification_report, confusion_matrix
import numpy as np

# ===== CONFIG =====
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
data_root = r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split'
checkpoint_path = r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_classifier_best.pth'
img_size = 224
batch_size = 32

# ===== DATA =====
test_transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(img_size),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

test_dataset = datasets.ImageFolder(os.path.join(data_root, 'test'), transform=test_transform)
test_loader = DataLoader(test_dataset, batch_size=batch_size, shuffle=False, num_workers=0, pin_memory=True)

classes = test_dataset.classes
print(f'Test samples: {len(test_dataset)}')
print(f'Classes: {classes}')

# ===== MODEL =====
model = models.efficientnet_b0(weights=None)
model.classifier[1] = nn.Linear(model.classifier[1].in_features, len(classes))
checkpoint = torch.load(checkpoint_path, map_location=device)
model.load_state_dict(checkpoint['model_state_dict'])
model = model.to(device)
model.eval()

print(f'Loaded model from epoch {checkpoint["epoch"]} (val_acc={checkpoint["val_acc"]:.2f}%)')

# ===== EVALUATION =====
criterion = nn.CrossEntropyLoss()
all_preds = []
all_labels = []
all_probs = []
test_loss = 0
correct = 0
total = 0

with torch.no_grad():
    for images, labels in test_loader:
        images, labels = images.to(device), labels.to(device)
        outputs = model(images)
        loss = criterion(outputs, labels)
        test_loss += loss.item()
        
        probs = torch.softmax(outputs, dim=1)
        _, predicted = outputs.max(1)
        
        total += labels.size(0)
        correct += predicted.eq(labels).sum().item()
        
        all_preds.extend(predicted.cpu().numpy())
        all_labels.extend(labels.cpu().numpy())
        all_probs.extend(probs.cpu().numpy())

test_loss /= len(test_loader)
test_acc = 100. * correct / total

print(f'\n=== TEST RESULTS ===')
print(f'Test Loss: {test_loss:.4f}')
print(f'Test Accuracy (Top-1): {test_acc:.2f}%')

# Top-3 accuracy
all_probs = np.array(all_probs)
top3_correct = 0
for i, label in enumerate(all_labels):
    top3 = np.argsort(all_probs[i])[-3:]
    if label in top3:
        top3_correct += 1
top3_acc = 100. * top3_correct / total
print(f'Top-3 Accuracy: {top3_acc:.2f}%')

# Classification report
print('\n=== PER-CLASS METRICS ===')
report = classification_report(all_labels, all_preds, target_names=classes, output_dict=True)
print(classification_report(all_labels, all_preds, target_names=classes, digits=4))

# Confusion matrix
cm = confusion_matrix(all_labels, all_preds)
print('\n=== CONFUSION MATRIX ===')
print('Labels:', classes)
print(cm)

# ===== SAVE RESULTS =====
results_dir = r'C:\Users\Luikz\Downloads\CloudScope\results'
os.makedirs(results_dir, exist_ok=True)

# Machine-readable
metrics = {
    'test_loss': test_loss,
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

with open(os.path.join(results_dir, 'metrics.json'), 'w') as f:
    json.dump(metrics, f, indent=2)

# Human-readable
with open(os.path.join(results_dir, 'test_report.md'), 'w') as f:
    f.write('# CloudScope Test Report\n\n')
    f.write(f'**Model**: EfficientNet-B0 (best checkpoint epoch {checkpoint["epoch"]})\n')
    f.write(f'**Test Samples**: {len(test_dataset)}\n')
    f.write(f'**Test Loss**: {test_loss:.4f}\n')
    f.write(f'**Top-1 Accuracy**: {test_acc:.2f}%\n')
    f.write(f'**Top-3 Accuracy**: {top3_acc:.2f}%\n')
    f.write(f'**Macro Precision**: {report["macro avg"]["precision"]:.4f}\n')
    f.write(f'**Macro Recall**: {report["macro avg"]["recall"]:.4f}\n')
    f.write(f'**Macro F1**: {report["macro avg"]["f1-score"]:.4f}\n\n')
    
    f.write('## Per-Class Metrics\n\n')
    f.write('| Class | Precision | Recall | F1-Score | Support |\n')
    f.write('|-------|-----------|--------|----------|--------|\n')
    for cls in classes:
        f.write(f'| {cls} | {report[cls]["precision"]:.4f} | {report[cls]["recall"]:.4f} | {report[cls]["f1-score"]:.4f} | {int(report[cls]["support"])} |\n')
    f.write(f'| **Macro Avg** | {report["macro avg"]["precision"]:.4f} | {report["macro avg"]["recall"]:.4f} | {report["macro avg"]["f1-score"]:.4f} | {int(report["macro avg"]["support"])} |\n')
    f.write(f'| **Weighted Avg** | {report["weighted avg"]["precision"]:.4f} | {report["weighted avg"]["recall"]:.4f} | {report["weighted avg"]["f1-score"]:.4f} | {int(report["weighted avg"]["support"])} |\n')
    
    f.write('\n## Confusion Matrix\n\n')
    f.write('Rows = True, Cols = Pred\n\n')
    f.write('| | ' + ' | '.join(classes) + ' |\n')
    f.write('|' + '---|' * (len(classes) + 1) + '\n')
    for i, cls in enumerate(classes):
        row = '| ' + cls + ' | ' + ' | '.join(str(x) for x in cm[i]) + ' |\n'
        f.write(row)

print(f'\nResults saved to {results_dir}/metrics.json and {results_dir}/test_report.md')