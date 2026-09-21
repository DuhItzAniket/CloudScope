import requests
import os

os.chdir(r'C:\Users\Luikz\Downloads\CloudScope\data\raw')

# Harvard Dataverse API
url = 'https://dataverse.harvard.edu/api/datasets/:persistentId/?persistentId=doi:10.7910/DVN/CADDPD'
print(f'Fetching dataset metadata from {url}')
response = requests.get(url)
print(f'Status: {response.status_code}')
if response.status_code == 200:
    data = response.json()
    files = data.get('data', {}).get('latestVersion', {}).get('files', [])
    print(f'Number of files: {len(files)}')
    for f in files[:20]:
        print(f"  {f['dataFile']['filename']}: {f['dataFile']['filesize']} bytes")