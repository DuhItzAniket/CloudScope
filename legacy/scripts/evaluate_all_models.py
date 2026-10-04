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

def get_model(model_name, num_classes):
    if model_name == 'efficientnet_b0':
        model = models.efficientnet_b0(weights=None)
        model.classifier[1] = nn.Linear(model.classifier[1].in_features, num_classes)
    elif model_name == 'resnet18':
        model = models.resnet18(weights=None)
        model.fc = nn.Linear(model.fc.in_features, num_classes)
    elif model_name == 'mobilenet_v3_small':
        model = models.mobilenet_v3_small(weights=None)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    elif model_name == 'mobilenet_v3_large':
        model = models.mobilenet_v3_large(weights=None)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    return model

def evaluate_model(model, name):
    model.eval()
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
    
    report = classification_report(all_labels, all_preds, target_names=classes, output_dict=True)
    cm = confusion_matrix(all_labels, all_preds)
    
    print(f'\n=== {name} TEST RESULTS ===')
    print(f'Test Accuracy (Top-1): {test_acc:.2f}%')
    print(f'Top-3 Accuracy: {top3_acc:.2f}%')
    print(f'Macro F1: {report["macro avg"]["f1-score"]:.4f}')
    print(classification_report(all_labels, all_preds, target_names=classes, digits=4))
    
    return {
        'model': name,
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

# Evaluate all models
models_to_test = [
    ('efficientnet_b0', r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_efficientnet_b0_best.pth'),
    ('resnet18', r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_resnet18_best.pth'),
    ('mobilenet_v3_small', r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_mobilenet_v3_small_best.pth'),
    ('mobilenet_v3_large', r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_mobilenet_v3_large_best.pth'),
]

all_results = {}

for model_name, checkpoint_path in models_to_test:
    if not os.path.exists(checkpoint_path):
        print(f'Checkpoint not found: {checkpoint_path}')
        continue
    
    print(f'\n--- Evaluating {model_name} ---')
    checkpoint = torch.load(checkpoint_path, map_location=device)
    model = get_model(model_name, len(classes))
    model.load_state_dict(checkpoint['model_state_dict'])
    model = model.to(device)
    
    results = evaluate_model(model, model_name)
    all_results[model_name] = results
    
    # Save individual results
    results_dir = r'C:\Users\Luikz\Downloads\CloudScope\results'
    os.makedirs(results_dir, exist_ok=True)
    with open(os.path.join(results_dir, f'metrics_{model_name}_test.json'), 'w') as f:
        json.dump(results, f, indent=2)

# Ensemble prediction (average probabilities)
print('\n=== ENSEMBLE EVALUATION ===')
ensemble_probs = None
for model_name, checkpoint_path in models_to_test:
    if not os.path.exists(checkpoint_path):
        continue
    checkpoint = torch.load(checkpoint_path, map_location=device)
    model = get_model(model_name, len(classes))
    model.load_state_dict(checkpoint['model_state_dict'])
    model = model.to(device)
    model.eval()
    
    model_probs = []
    with torch.no_grad():
        for images, _ in test_loader:
            images = images.to(device)
            outputs = model(images)
            probs = torch.softmax(outputs, dim=1)
            model_probs.extend(probs.cpu().numpy())
    
    model_probs = np.array(model_probs)
    if ensemble_probs is None:
        ensemble_probs = model_probs
    else:
        ensemble_probs += model_probs

ensemble_probs /= len([m for m, _ in models_to_test if os.path.exists(m[1])])

# Ensemble predictions
all_labels = []
for _, labels in test_loader:
    all_labels.extend(labels.numpy())

ensemble_preds = np.argmax(ensemble_probs, axis=1)
ensemble_correct = sum(p == l for p, l in zip(ensemble_preds, all_labels))
ensemble_acc = 100. * ensemble_correct / len(all_labels)

# Top-3
top3_correct = 0
for i, label in enumerate(all_labels):
    top3 = np.argsort(ensemble_probs[i])[-3:]
    if label in top3:
        top3_correct += 1
ensemble_top3 = 100. * top3_correct / len(all_labels)

report = classification_report(all_labels, ensemble_preds, target_names=classes, output_dict=True)
cm = confusion_matrix(all_labels, ensemble_preds)

print(f'Ensemble Test Accuracy (Top-1): {ensemble_acc:.2f}%')
print(f'Ensemble Top-3 Accuracy: {ensemble_top3:.2f}%')
print(f'Ensemble Macro F1: {report["macro avg"]["f1-score"]:.4f}')
print(classification_report(all_labels, ensemble_preds, target_names=classes, digits=4))

ensemble_results = {
    'model': 'ensemble_avg',
    'test_acc_top1': ensemble_acc,
    'test_acc_top3': ensemble_top3,
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

all_results['ensemble'] = ensemble_results

# Save all results
with open(os.path.join(results_dir, 'metrics_all_models.json'), 'w') as f:
    json.dump(all_results, f, indent=2)

print('\n=== SUMMARY ===')
for name, res in all_results.items():
    print(f'{name:30s} Top-1: {res["test_acc_top1"]:.2f}%  Top-3: {res["test_acc_top3"]:.2f}%  Macro F1: {res["macro_f1"]:.4f}')