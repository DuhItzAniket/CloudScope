# CloudScope Dataset Notes

## Candidate Datasets for Phase 1

### 1. CCSN (Cirrus Cumulus Stratus Nimbus) — SELECTED FOR FIRST EXPERIMENT
- **Source**: Harvard Dataverse (doi:10.7910/DVN/CADDPD)
- **GitHub**: https://github.com/upuil/CCSN-Database
- **Images**: 2,543 total
- **Classes**: 11 (WMO genera + contrail)
  - Ci (cirrus): 139
  - Cs (cirrostratus): 287
  - Cc (cirrocumulus): 268
  - Ac (altocumulus): 221
  - As (altostratus): 188
  - Cu (cumulus): 182
  - Cb (cumulonimbus): 242
  - Ns (nimbostratus): 274
  - Sc (stratocumulus): 340
  - St (stratus): 202
  - Ct (contrail): 200
- **Resolution**: 256×256 JPEG
- **License**: Public (Harvard Dataverse)
- **Imbalance**: Significant (139–340 per class)
- **Suitability**: Excellent for first baseline — small, standard benchmark, well-documented

### 2. DeepSky
- **Source**: GitHub (dimkastan/DeepSky-classification-dataset)
- **Images**: 7,000+ over 2 years
- **Classes**: 7 (merged WMO)
- **Resolution**: 678×678 (cropped from fisheye)
- **Split**: Year-based (2021 train/val, 2022 test)
- **License**: Public
- **Note**: Good for future cross-dataset evaluation

### 3. TJNU GCD (Ground-based Cloud Dataset)
- **Source**: GitHub (shuangliutjnu/TJNU-Ground-based-Cloud-Dataset)
- **Images**: 19,000
- **Classes**: 7 (merged WMO)
- **Resolution**: 512×512
- **Split**: 10,000 train / 9,000 test
- **License**: Requires agreement
- **Note**: Larger, but requires agreement — skip for now

### 4. LenghuSky-8
- **Source**: Hugging Face (ruiyicheng/LenghuSky-8), GitHub (ruiyicheng/LenghuSky-8)
- **Images**: 429,620 (8 years), 512×512
- **Focus**: Segmentation (cloud/sky/contamination), nowcasting, astrometric calibration
- **Pretrained**: DINOv3 linear probe available
- **Size**: ~20GB processed images, ~40GB logits
- **Note**: For Phase 8 segmentation feasibility only — too large for initial classification

### 5. Clouds-1000/1500
- **Source**: Mendeley Data, GitHub (bjuncklaus/Clouds-1000)
- **Images**: 1,000 / 1,500
- **Classes**: 4 height-based + background
- **View**: Horizon-facing (not all-sky)
- **License**: CC BY 4.0
- **Note**: Different camera geometry — not suitable for all-sky classification

---

## Decision: CCSN for Phase 1–7

**Rationale**:
- Smallest scientifically sensible dataset (2,543 images)
- Standard 11-class WMO taxonomy
- Public, no agreement needed
- Well-established benchmark (many papers)
- 256×256 fits 6GB VRAM easily
- Class imbalance is a realistic challenge to handle

**Download URL**: https://doi.org/10.7910/DVN/CADDPD (or GitHub mirror)

**Preprocessing needed**:
- Verify all 2,543 images load correctly
- Check for corrupt files
- Stratified train/val/test split (70/15/15) by class
- No augmentation on val/test
- Standard ImageNet normalization for transfer learning