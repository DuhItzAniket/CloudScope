from huggingface_hub import HfApi
api = HfApi()
datasets = api.list_datasets(search='cloud', limit=20)
for d in datasets:
    tags = d.tags[:5] if d.tags else "no tags"
    print(f'{d.id}: {tags}')