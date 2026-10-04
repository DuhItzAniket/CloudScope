# Ph29: Camera Source Matrix — DONE

| Source | Mechanism | Verified |
|--------|-----------|----------|
| USB / built-in (index) | MSMF `VideoCapture(idx)` | ✅ Cam 0 640x480 grabbed=5 live |
| Video file | same API (CV_IMAGES/FFMPEG) | ✅ B0268 jpg opens+reads; missing file fails honestly |
| IP/RTSP | same API, URL string | ⚠️ Implemented, NOT live-tested (no RTSP server on LAN) |
| Absent camera | probe reports n/a | ✅ Indices 1-4 correctly n/a |

- Disconnect path: `read()` false → worker logs + stops cleanly (no crash path).
- QML Start routes index/RTSP/file by content sniff (URL/file vs index).
