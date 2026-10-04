# Cloud-Base Height Estimation Feasibility

## Status: NOT FEASIBLE for Supervised Training (Currently)

**No publicly available dataset with paired [all-sky image + verified cloud-base height] found suitable for supervised baseline training.**

## Candidate Data Sources

| Source | Has Images | Has Height Data | Paired? | Accessible | Notes |
|--------|------------|-----------------|---------|------------|-------|
| ARM (DOE) | ✅ All-sky cameras | ✅ Ceilometers | ⚠️ Partial | Yes (free) | Multiple sites, need synchronization |
| SURFRAD (NOAA) | ❌ | ✅ Cloud properties | ❌ | Yes | Radiation-focused, no all-sky cameras |
| CloudNet (EU) | ❌ | ✅ Ceilometer/lidar | ❌ | Yes | Ground-based remote sensing |
| CCSN | ✅ | ❌ | ❌ | Yes | Classification only |
| LenghuSky-8 | ✅ | ❌ | ❌ | Yes | Segmentation only |
| DeepSky | ✅ | ❌ | ❌ | Yes | Classification only |
| TJNU GCD | ✅ | ❌ | ❌ | Agreement | Classification only |

## Technical Challenges

1. **Temporal Synchronization**: Camera (1/min) vs Ceilometer (10-30 sec) - need ±30 sec alignment
2. **Spatial Mismatch**: Ceilometer measures vertical column; camera sees full hemisphere
3. **Multi-layer Clouds**: Ceilometer detects multiple bases; which one corresponds to visible cloud?
4. **Clear Sky**: No cloud base height defined
5. **Oblique Views**: All-sky camera sees clouds at various zenith angles

## Required Setup for Future Work

- Co-located all-sky camera + Vaisala ceilometer (or similar)
- Synchronized timestamps (NTP/GPS)
- Continuous recording for months
- Manual QA of paired samples

## Alternative Approaches (Not Supervised Regression)

1. **Physics-based**: Use segmentation + camera geometry + sun position to estimate height from shadow/cloud edge
2. **Stereo**: Two cameras at known baseline
3. **Class-based priors**: Assign typical height ranges per cloud class (Ci: 5-13km, Cu: 0.5-2km, etc.)
4. **Transfer from satellite**: Train on satellite IR brightness temperature → height, adapt to ground

## Recommendation

**Do not attempt supervised altitude regression** without dedicated data collection campaign.
Focus on classification + segmentation first. Altitude estimation requires:
- Dedicated hardware deployment (camera + ceilometer)
- 3-6 months data collection
- Significant engineering effort

---
*Generated: 2026-09-21*