import requests
import json

url = 'https://zenodo.org/api/records/8208505'
response = requests.get(url)
print(f'Status: {response.status_code}')
if response.status_code == 200:
    data = response.json()
    files = data.get('files', [])
    for f in files:
        print(f"{f['key']}: {f['size']/1e6:.1f} MB - {f['links']['self']}")