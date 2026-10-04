import os
import requests
from PIL import Image
import io

ext_dir = r'C:\Users\Luikz\Downloads\CloudScope\data\external_test'
os.makedirs(ext_dir, exist_ok=True)

# Try to download sample images from CCSN GitHub
urls = [
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/0-N002.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/1-N023.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/2-N098.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/3-N087.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/4-N045.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/5-N073.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/6-N087.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/7-N002.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/8-N055.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/9-N143.jpg',
    'https://raw.githubusercontent.com/upuil/CCSN-Database/main/sample%20images/10-N023.jpg',
]

for i, url in enumerate(urls):
    try:
        response = requests.get(url, timeout=10)
        if response.status_code == 200:
            img = Image.open(io.BytesIO(response.content))
            img = img.convert('RGB')
            save_path = os.path.join(ext_dir, f'external_{i:02d}.jpg')
            img.save(save_path)
            print(f'Downloaded {save_path} ({img.size})')
    except Exception as e:
        print(f'Failed {url}: {e}')

jpg_files = [f for f in os.listdir(ext_dir) if f.endswith('.jpg')]
print(f'Total external images: {len(jpg_files)}')