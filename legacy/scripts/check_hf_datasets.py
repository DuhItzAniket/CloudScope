from huggingface_hub import HfApi
api = HfApi()

# Check Francesco/cloud-types
info = api.dataset_info('Francesco/cloud-types')
print(f'ID: {info.id}')
print(f'Description: {info.description[:500] if info.description else "No description"}')
print(f'Tags: {info.tags}')

# Check csaybar/CloudSEN12-high
info2 = api.dataset_info('csaybar/CloudSEN12-high')
print(f'\nID: {info2.id}')
print(f'Description: {info2.description[:500] if info2.description else "No description"}')
print(f'Tags: {info2.tags}')