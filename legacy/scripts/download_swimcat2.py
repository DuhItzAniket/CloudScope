import requests
import zipfile
import io
import os

os.chdir(r'C:\Users\Luikz\Downloads\CloudScope\data\raw')

url = 'http://vintage.winklerbros.net/swimcat.zip'
print(f'Trying SWIMCAT: {url}')
response = requests.get(url, stream=True, allow_redirects=True, timeout=30)
print(f'Status: {response.status_code}')
ct = response.headers.get("content-type")
print(f'Content-Type: {ct}')
cl = response.headers.get("content-length")
print(f'Content-Length: {cl}')

if response.status_code == 200 and cl and int(cl) < 100 * 1e6:
    with zipfile.ZipFile(io.BytesIO(response.content)) as z:
        z.extractall('.')
        print(f'Extracted {len(z.namelist())} files')