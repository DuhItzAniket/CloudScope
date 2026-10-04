# CloudScope Dataset Report

## Dataset: CCSN (Cirrus Cumulus Stratus Nimbus)
- **Source**: GitHub mirror of Harvard Dataverse (doi:10.7910/DVN/CADDPD)
- **Total Images**: 2,543
- **Classes**: 11 (WMO genera + contrail)
- **Resolution**: Mixed (mostly 400×400, some 256×256)
- **Format**: JPEG

## Class Distribution (Original)

| Class | Code | Count | Percentage |
|-------|------|-------|------------|
| Altocumulus | Ac | 221 | 8.7% |
| Altostratus | As | 188 | 7.4% |
| Cumulonimbus | Cb | 242 | 9.5% |
| Cirrocumulus | Cc | 268 | 10.5% |
| Cirrus | Ci | 139 | 5.5% |
| Cirrostratus | Cs | 287 | 11.3% |
| Contrail | Ct | 200 | 7.9% |
| Cumulus | Cu | 182 | 7.2% |
| Nimbostratus | Ns | 274 | 10.8% |
| Stratocumulus | Sc | 340 | 13.4% |
| Stratus | St | 202 | 7.9% |

## Data Quality
- **Corrupt images**: 0
- **Resolution inconsistency**: 3 classes (Cc, Cs, Ns) have mixed 256×256 and 400×400
- **Duplicates**: Not checked (filename-based only)

## Train/Validation/Test Split (70/15/15, Stratified)

| Class | Train | Val | Test |
|-------|-------|-----|------|
| Ac | 154 | 33 | 34 |
| As | 131 | 28 | 29 |
| Cb | 169 | 36 | 37 |
| Cc | 187 | 40 | 41 |
| Ci | 97 | 20 | 22 |
| Cs | 200 | 43 | 44 |
| Ct | 140 | 30 | 30 |
| Cu | 127 | 27 | 28 |
| Ns | 191 | 41 | 42 |
| Sc | 237 | 51 | 52 |
| St | 141 | 30 | 31 |
| **Total** | **1,774** | **379** | **390** |

## Preprocessing Plan
- Resize all images to 224×224 (standard for ImageNet pretrained models)
- Normalize with ImageNet mean/std: mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]
- Training augmentation: RandomResizedCrop(224), RandomHorizontalFlip, ColorJitter
- Validation/Test: Resize(256), CenterCrop(224)

## Location
- Raw: `data/raw/ccsn/`
- Processed splits: `data/processed/ccsn_split/`