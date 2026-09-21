import os
import random
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

random.seed(42)

root = r'C:\Users\Luikz\Downloads\CloudScope\data\raw\ccsn'
classes = sorted([d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))])

fig, axes = plt.subplots(3, 4, figsize=(16, 12))
axes = axes.flatten()

for i, cls in enumerate(classes):
    cls_dir = os.path.join(root, cls)
    files = [f for f in os.listdir(cls_dir) if f.lower().endswith(('.jpg', '.jpeg', '.png'))]
    sample = random.choice(files)
    img = Image.open(os.path.join(cls_dir, sample))
    axes[i].imshow(img)
    axes[i].set_title(f'{cls} ({sample})', fontsize=10)
    axes[i].axis('off')

# Hide unused subplot
axes[-1].axis('off')

plt.suptitle('CCSN Dataset - Sample Images per Class', fontsize=16)
plt.tight_layout()
plt.savefig(r'C:\Users\Luikz\Downloads\CloudScope\results\dataset_samples.png', dpi=150, bbox_inches='tight')
print('Saved dataset_samples.png')