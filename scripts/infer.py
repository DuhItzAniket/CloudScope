#!/usr/bin/env python
"""
CloudScope Inference Script
Usage: python scripts/infer.py --image path/to/image.jpg
"""
import os
import sys
import argparse
import json
import time
import torch
import torch.nn as nn
from torchvision import transforms, models
from PIL import Image

# Default paths
DEFAULT_CHECKPOINT = r'C:\Users\Luikz\Downloads\CloudScope\models\cloudscope_classifier_best.pth'
DEFAULT_IMG_SIZE = 224

def load_model(checkpoint_path, device):
    """Load model from checkpoint."""
    checkpoint = torch.load(checkpoint_path, map_location=device)
    classes = checkpoint['classes']
    num_classes = len(classes)
    
    model = models.efficientnet_b0(weights=None)
    model.classifier[1] = nn.Linear(model.classifier[1].in_features, num_classes)
    model.load_state_dict(checkpoint['model_state_dict'])
    model = model.to(device)
    model.eval()
    
    return model, classes, checkpoint

def predict(model, classes, image_path, img_size, device):
    """Run inference on a single image."""
    transform = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(img_size),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])
    
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
    
    return {
        'image': image_path,
        'predicted_class': top3[0][0],
        'confidence': top3[0][1],
        'top3': top3,
        'inference_time_ms': inference_time
    }

def main():
    parser = argparse.ArgumentParser(description='CloudScope cloud classification inference')
    parser.add_argument('--image', required=True, help='Path to input image')
    parser.add_argument('--checkpoint', default=DEFAULT_CHECKPOINT, help='Path to model checkpoint')
    parser.add_argument('--img-size', type=int, default=DEFAULT_IMG_SIZE, help='Input image size')
    parser.add_argument('--json', action='store_true', help='Output JSON instead of formatted text')
    args = parser.parse_args()
    
    if not os.path.exists(args.image):
        print(f'Error: Image not found: {args.image}', file=sys.stderr)
        sys.exit(1)
    
    if not os.path.exists(args.checkpoint):
        print(f'Error: Checkpoint not found: {args.checkpoint}', file=sys.stderr)
        sys.exit(1)
    
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f'Device: {device}', file=sys.stderr)
    
    model, classes, checkpoint = load_model(args.checkpoint, device)
    
    result = predict(model, classes, args.image, args.img_size, device)
    
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f'Image: {result["image"]}')
        print(f'Predicted cloud class: {result["predicted_class"]}')
        print(f'Confidence: {result["confidence"]:.4f}')
        print('Top-3 predictions:')
        for cls, conf in result['top3']:
            print(f'  {cls}: {conf:.4f}')
        print(f'Inference time: {result["inference_time_ms"]:.1f} ms')

if __name__ == '__main__':
    main()