import os
import json
import time
import torch
import torch.nn as nn
from torchvision import transforms, models
from PIL import Image

# ===== CONFIG =====
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
checkpoint_path = r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_classifier_best.pth'
img_size = 224

# ===== LOAD MODEL =====
checkpoint = torch.load(checkpoint_path, map_location=device)
classes = checkpoint['classes']
num_classes = len(classes)

model = models.efficientnet_b0(weights=None)
model.classifier[1] = nn.Linear(model.classifier[1].in_features, num_classes)
model.load_state_dict(checkpoint['model_state_dict'])
model = model.to(device)
model.eval()

print(f'Loaded model (epoch {checkpoint["epoch"]}, val_acc={checkpoint["val_acc"]:.2f}%)')
print(f'Classes: {classes}')

# ===== TRANSFORM =====
transform = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(img_size),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

def predict(image_path):
    """Run inference on a single image."""
    img = Image.open(image_path).convert('RGB')
    input_tensor = transform(img).unsqueeze(0).to(device)
    
    start = time.time()
    with torch.no_grad():
        outputs = model(input_tensor)
        probs = torch.softmax(outputs, dim=1)
    inference_time = (time.time() - start) * 1000  # ms
    
    # Top-3
    top3_probs, top3_idx = probs.topk(3, dim=1)
    top3 = [(classes[idx.item()], top3_probs[0, i].item()) for i, idx in enumerate(top3_idx[0])]
    
    pred_class = top3[0][0]
    pred_conf = top3[0][1]
    
    return {
        'image': image_path,
        'predicted_class': pred_class,
        'confidence': pred_conf,
        'top3': top3,
        'inference_time_ms': inference_time
    }

# ===== TEST ON EXTERNAL IMAGES =====
ext_dir = r'C:\Users\Luikz\Downloads\CloudScope\data\external_test'
jpg_files = [f for f in os.listdir(ext_dir) if f.lower().endswith(('.jpg', '.jpeg', '.png'))]

print(f'\n=== EXTERNAL TEST ({len(jpg_files)} images) ===')
results = []
for f in sorted(jpg_files):
    path = os.path.join(ext_dir, f)
    result = predict(path)
    results.append(result)
    print(f'{f}: {result["predicted_class"]} ({result["confidence"]:.3f}) | Top3: {", ".join(f"{c}:{p:.3f}" for c,p in result["top3"])} | {result["inference_time_ms"]:.1f}ms')

# Save results
results_dir = r'C:\Users\Luikz\Downloads\CloudScope\results'
os.makedirs(results_dir, exist_ok=True)

with open(os.path.join(results_dir, 'external_test_results.json'), 'w') as f:
    json.dump(results, f, indent=2)

print(f'\nExternal test results saved to {results_dir}/external_test_results.json')