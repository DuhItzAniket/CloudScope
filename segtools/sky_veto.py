"""Conservative blue-sky veto: flips learned-cloud -> sky ONLY for pixels that
are unambiguously saturated blue sky (B-R>50, B>140, S>0.35 in HSV).

Never creates cloud predictions; can only remove false-cloud. Heuristic,
documented as such — compensates for zero vivid-blue-sky in LenghuSky labels.
"""
import numpy as np
import cv2


def veto_mask(img_rgb_u8, mask, b_minus_r=50, b_min=140, s_min=0.35):
    """img: HxWx3 uint8 RGB; mask: HxW uint8 {0 sky,1 cloud,2 contam}. Returns fixed mask."""
    out = mask.copy()
    bgr = cv2.cvtColor(img_rgb_u8, cv2.COLOR_RGB2BGR)
    hsv = cv2.cvtColor(img_rgb_u8, cv2.COLOR_RGB2HSV).astype(np.float32)
    b, g, r = bgr[:, :, 0].astype(np.int16), bgr[:, :, 1], bgr[:, :, 2].astype(np.int16)
    s = hsv[:, :, 1] / 255.0
    is_blue_sky = (b - r > b_minus_r) & (b > b_min) & (s > s_min)
    flip = is_blue_sky & (mask == 1)
    out[flip] = 0
    return out, {'flipped_frac': round(float(flip.mean()), 4)}
