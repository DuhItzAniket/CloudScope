"""Per-frame image statistics recorded in each sidecar.

Statistics are computed on a copy downscaled to at most STATS_MAX_DIM pixels
on the long side (recorded in the sidecar) so they stay cheap on a Raspberry Pi.
"""

from __future__ import annotations

import cv2
import numpy as np

STATS_MAX_DIM = 1024
CLIP_LEVEL = 254   # a channel value >= this counts as clipped (8-bit)
DARK_LEVEL = 1


def _downscale(bgr: np.ndarray) -> np.ndarray:
    h, w = bgr.shape[:2]
    scale = STATS_MAX_DIM / max(h, w)
    if scale >= 1.0:
        return bgr
    return cv2.resize(bgr, (round(w * scale), round(h * scale)), interpolation=cv2.INTER_AREA)


def frame_stats(bgr: np.ndarray) -> dict:
    """Exposure and quality statistics for an 8-bit BGR frame."""
    if bgr.dtype != np.uint8 or bgr.ndim != 3 or bgr.shape[2] != 3:
        raise ValueError(f"expected 8-bit BGR image, got dtype={bgr.dtype} shape={bgr.shape}")
    small = _downscale(bgr)
    luma = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
    clipped = (small >= CLIP_LEVEL).any(axis=2)
    p01, p50, p99 = np.percentile(luma, [1, 50, 99])

    # Largest clipped blob: usually the Sun or glare around it.
    n, _, cc_stats, centroids = cv2.connectedComponentsWithStats(clipped.astype(np.uint8), connectivity=8)
    blob = None
    if n > 1:
        i = 1 + int(np.argmax(cc_stats[1:, cv2.CC_STAT_AREA]))
        sx = bgr.shape[1] / small.shape[1]
        sy = bgr.shape[0] / small.shape[0]
        blob = {
            "area_fraction": float(cc_stats[i, cv2.CC_STAT_AREA] / clipped.size),
            "centroid_xy": [float(centroids[i][0] * sx), float(centroids[i][1] * sy)],  # full-resolution pixels
        }

    b, g, r = (float(small[..., c].mean()) for c in range(3))
    return {
        "stats_resolution": [int(small.shape[1]), int(small.shape[0])],
        "mean_rgb": [r, g, b],
        "luma_p01_p50_p99": [float(p01), float(p50), float(p99)],
        "clipped_fraction": float(clipped.mean()),
        "dark_fraction": float((small <= DARK_LEVEL).all(axis=2).mean()),
        "sharpness_laplacian_var": float(cv2.Laplacian(luma, cv2.CV_64F).var()),
        "largest_clipped_blob": blob,
    }
