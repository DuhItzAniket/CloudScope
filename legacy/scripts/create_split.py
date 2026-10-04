import os
import random
import json
import shutil
from collections import defaultdict

random.seed(42)

root = r'C:\Users\Luikz\Downloads\CloudScope\data\raw\ccsn'
split_dir = r'C:\Users\Luikz\Downloads\CloudScope\data\processed\ccsn_split'

os.makedirs(os.path.join(split_dir, 'train'), exist_ok=True)
os.makedirs(os.path.join(split_dir, 'val'), exist_ok=True)
os.makedirs(os.path.join(split_dir, 'test'), exist_ok=True)

all_data = []
for cls in sorted(os.listdir(root)):
    cls_dir = os.path.join(root, cls)
    if not os.path.isdir(cls_dir):
        continue
    for f in os.listdir(cls_dir):
        if f.lower().endswith(('.jpg', '.jpeg', '.png')):
            all_data.append((cls, f))

# Stratified split by class
class_data = defaultdict(list)
for cls, fname in all_data:
    class_data[cls].append(fname)

split_info = {'train': [], 'val': [], 'test': []}

for cls, files in class_data.items():
    random.shuffle(files)
    n = len(files)
    n_train = int(n * 0.7)
    n_val = int(n * 0.15)
    
    train_files = files[:n_train]
    val_files = files[n_train:n_train + n_val]
    test_files = files[n_train + n_val:]
    
    for split, flist in [('train', train_files), ('val', val_files), ('test', test_files)]:
        split_cls_dir = os.path.join(split_dir, split, cls)
        os.makedirs(split_cls_dir, exist_ok=True)
        for f in flist:
            src = os.path.join(root, cls, f)
            dst = os.path.join(split_cls_dir, f)
            shutil.copy2(src, dst)
        split_info[split].extend([(cls, f) for f in flist])
        print(f'{cls}: train={len(train_files)}, val={len(val_files)}, test={len(test_files)}')

# Save split info
with open(os.path.join(split_dir, 'split_info.json'), 'w') as f:
    json.dump(split_info, f, indent=2)

print('Total: train=%d, val=%d, test=%d' % (len(split_info['train']), len(split_info['val']), len(split_info['test'])))