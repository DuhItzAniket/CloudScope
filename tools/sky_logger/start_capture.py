"""One-command sky capture for STRATIA data collection with the Arducam B0268.

Edit the SETTINGS block once, then double-click this file or run:
    python start_capture.py
It finds the camera that delivers the B0268's full 4656 x 3496 resolution (trying the Media Foundation
and DirectShow backends and indexes 0-3), then starts the sky logger with the recommended settings.
Stop it with Ctrl+C in the evening; run it again the next morning. Night frames are skipped automatically.
"""

import subprocess
import sys
from pathlib import Path

import cv2

# ---------------------------------------------------------------- SETTINGS (edit these) -----------
OUT_DIR = r"C:\CloudScopeCaptures"   # where the frames go (needs a few GB per day)
SITE = "BLR01"                       # short site code, keep it the same every day
LAT, LON, ALT_M = 12.97, 77.59, 920  # your real site: latitude, longitude (degrees), altitude (m)
INTERVAL_S = 30                      # seconds between frames
POINTING_NOTE = "zenith, fixed mount"
# --------------------------------------------------------------------------------------------------

WIDTH, HEIGHT = 4656, 3496
BACKENDS = [("msmf", cv2.CAP_MSMF), ("dshow", cv2.CAP_DSHOW)]


def find_b0268() -> tuple[int, str] | None:
    """Return (index, backend name) of the first camera that delivers a full-resolution MJPG frame."""
    for name, flag in BACKENDS:
        for index in range(4):
            cap = cv2.VideoCapture(index, flag)
            if not cap.isOpened():
                cap.release()
                continue
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
            ok, frame = cap.read()
            cap.release()
            if ok and frame is not None and frame.shape[1] == WIDTH and frame.shape[0] == HEIGHT:
                print(f"B0268 found: camera {index} via {name} ({WIDTH}x{HEIGHT})")
                return index, name
            shape = None if frame is None else (frame.shape[1], frame.shape[0])
            print(f"camera {index} via {name}: {'no frame' if not ok else shape} - not it")
    return None


def main() -> int:
    here = Path(__file__).resolve().parent
    Path(OUT_DIR).mkdir(parents=True, exist_ok=True)
    found = find_b0268()
    if found is None:
        print("\nNo camera delivered 4656x3496. Is the B0268 plugged in? Try another USB port, then run again.")
        return 2
    index, backend = found
    cmd = [
        sys.executable, "-m", "sky_logger", "run",
        "--camera", str(index), "--backend", backend,
        "--fourcc", "MJPG", "--width", str(WIDTH), "--height", str(HEIGHT),
        "--out", OUT_DIR, "--site", SITE, "--interval", str(INTERVAL_S),
        "--lat", str(LAT), "--lon", str(LON), "--alt", str(ALT_M),
        "--min-sun-elevation", "-6",
        "--pointing", POINTING_NOTE, "--pointing-el", "90",
    ]
    print("\nStarting the logger (Ctrl+C to stop):\n  " + " ".join(cmd[1:]) + "\n")
    return subprocess.call(cmd, cwd=here)


if __name__ == "__main__":
    sys.exit(main())
