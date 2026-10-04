import requests
import zipfile
import io
import os

os.chdir(r'C:\Users\Luikz\Downloads\CloudScope\data\raw')

# Try SWIMCAT-ext from Mendeley
url = 'https://data.mendeley.com/datasets/vwdd9grvdp/1/files/4e8c4e8c-4e8c-4e8c-4e8c-4e8c4e8c4e8c/SWIMCAT-ext.zip?download=1'
print(f'Trying SWIMCAT-ext from Mendeley')
response = requests.get(url, stream=True, allow_redirects=True)
print(f'Status: {response.status_code}')
print(f'Content-Type: {response.headers.get("content-type")}')
cl = response.headers.get("content-length")
print(f'Content-Length: {cl}')

if response.status_code == 200 and cl and int(cl) < 500 * 1e6:
    with zipfile.ZipFile(io.BytesIO(response.content)) as z:
        z.extractall('.')
        print(f'Extracted {len(z.namelist())} files')