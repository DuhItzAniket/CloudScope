"""Camera access through OpenCV, with honest read-back of every control.

UVC control semantics differ between OpenCV backends, so the logger never
assumes a setting took effect: it records what was requested, whether OpenCV
accepted it, and what the driver reports afterwards. `probe --test-exposure`
measures whether exposure control actually changes the image.
"""

from __future__ import annotations

import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone

import cv2
import numpy as np

BACKENDS = {"auto": cv2.CAP_ANY, "dshow": cv2.CAP_DSHOW, "msmf": cv2.CAP_MSMF, "v4l2": cv2.CAP_V4L2}

# Value written to CAP_PROP_AUTO_EXPOSURE to request manual / automatic exposure.
# These are the conventions used by OpenCV's V4L2 backend (1 = manual, 3 = aperture
# priority) and the widely used 0.25 / 0.75 convention for DirectShow and MSMF.
# Whether a given driver honours them is checked by read-back and by probe --test-exposure.
AUTO_EXPOSURE_VALUES = {
    "v4l2": {"manual": 1.0, "auto": 3.0},
    "dshow": {"manual": 0.25, "auto": 0.75},
    "msmf": {"manual": 0.25, "auto": 0.75},
}

READABLE_PROPS = {
    "width": cv2.CAP_PROP_FRAME_WIDTH,
    "height": cv2.CAP_PROP_FRAME_HEIGHT,
    "fps": cv2.CAP_PROP_FPS,
    "fourcc": cv2.CAP_PROP_FOURCC,
    "buffersize": cv2.CAP_PROP_BUFFERSIZE,
    "auto_exposure": cv2.CAP_PROP_AUTO_EXPOSURE,
    "exposure": cv2.CAP_PROP_EXPOSURE,
    "gain": cv2.CAP_PROP_GAIN,
    "auto_wb": cv2.CAP_PROP_AUTO_WB,
    "wb_temperature": cv2.CAP_PROP_WB_TEMPERATURE,
    "brightness": cv2.CAP_PROP_BRIGHTNESS,
    "contrast": cv2.CAP_PROP_CONTRAST,
    "saturation": cv2.CAP_PROP_SATURATION,
    "sharpness": cv2.CAP_PROP_SHARPNESS,
    "gamma": cv2.CAP_PROP_GAMMA,
}


def default_backend() -> str:
    if sys.platform == "win32":
        return "dshow"   # exposes UVC exposure/gain controls and the driver settings dialog
    if sys.platform.startswith("linux"):
        return "v4l2"
    return "auto"


def fourcc_to_str(value: float) -> str | None:
    v = int(value)
    if v <= 0:
        return None
    s = "".join(chr((v >> (8 * i)) & 0xFF) for i in range(4))
    return s if s.isprintable() else None


def exposure_seconds_estimate(backend: str, exposure: float | None) -> float | None:
    """Convert a raw exposure value to seconds where the backend's unit is documented."""
    if exposure is None:
        return None
    if backend == "dshow" and exposure <= 0:
        return 2.0 ** exposure            # DirectShow CameraControl_Exposure is log2(seconds)
    if backend == "v4l2" and exposure > 0:
        return exposure * 100e-6          # V4L2 exposure_time_absolute is in 100 us units
    return None


@dataclass
class CameraConfig:
    index: int = 0
    backend: str = field(default_factory=default_backend)
    width: int | None = None
    height: int | None = None
    fourcc: str | None = None             # None = keep the driver's default pixel format
    fps: float | None = None
    auto_exposure: str = "leave"          # "leave" | "manual" | "auto"
    exposure: float | None = None         # raw backend units
    gain: float | None = None
    auto_wb: str = "leave"                # "leave" | "manual" | "auto"
    wb_temperature: float | None = None
    flush_frames: int = 3                 # frames discarded before each capture to avoid stale buffers

    def requested(self) -> dict:
        return {k: v for k, v in self.__dict__.items() if k not in ("index", "flush_frames")}


class Camera:
    def __init__(self, cfg: CameraConfig):
        if cfg.backend not in BACKENDS:
            raise ValueError(f"unknown backend {cfg.backend!r}; choose from {sorted(BACKENDS)}")
        self.cfg = cfg
        self.cap: cv2.VideoCapture | None = None
        self.control_report: dict = {}

    # -- lifecycle -------------------------------------------------------
    def open(self) -> None:
        """Open and configure the camera, then prove it delivers a frame.

        If a requested pixel format leaves the camera unable to deliver frames (seen on
        laptop webcams with MJPG), reopen with the driver default and record the fallback.
        """
        self._open_raw()
        self.control_report = self._apply_controls()
        if self._test_grab():
            return
        if self.cfg.fourcc:
            requested = self.control_report.get("fourcc", {})
            self._open_raw()
            self.control_report = self._apply_controls(skip_fourcc=True)
            if self._test_grab():
                self.control_report["fourcc"] = {
                    **requested,
                    "fallback": "no frames with requested pixel format; reopened with driver default",
                    "readback_str": fourcc_to_str(self._get(cv2.CAP_PROP_FOURCC) or 0),
                }
                return
        self.release()
        raise RuntimeError(f"camera index {self.cfg.index} ({self.cfg.backend}) opened but delivers no frames")

    def _open_raw(self) -> None:
        self.release()
        cap = cv2.VideoCapture(self.cfg.index, BACKENDS[self.cfg.backend])
        if not cap.isOpened():
            raise RuntimeError(f"cannot open camera index {self.cfg.index} with backend {self.cfg.backend}")
        self.cap = cap

    def _test_grab(self, attempts: int = 5) -> bool:
        for _ in range(attempts):
            ok, frame = self.cap.read()
            if ok and frame is not None and frame.size:
                return True
        return False

    def release(self) -> None:
        if self.cap is not None:
            self.cap.release()
            self.cap = None

    def __enter__(self) -> "Camera":
        self.open()
        return self

    def __exit__(self, *exc) -> None:
        self.release()

    # -- controls --------------------------------------------------------
    def _set(self, name: str, prop: int, value: float) -> dict:
        ok = bool(self.cap.set(prop, value))
        return {"requested": value, "accepted": ok, "readback": self._get(prop)}

    def _get(self, prop: int) -> float | None:
        v = self.cap.get(prop)
        return None if v is None or (isinstance(v, float) and np.isnan(v)) else float(v)

    def _apply_controls(self, skip_fourcc: bool = False) -> dict:
        c, rep = self.cfg, {}
        # Order matters on DirectShow: pixel format before resolution, mode before value.
        if c.fourcc and not skip_fourcc:
            rep["fourcc"] = self._set("fourcc", cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*c.fourcc))
            rep["fourcc"]["readback_str"] = fourcc_to_str(rep["fourcc"]["readback"] or 0)
        if c.width:
            rep["width"] = self._set("width", cv2.CAP_PROP_FRAME_WIDTH, c.width)
        if c.height:
            rep["height"] = self._set("height", cv2.CAP_PROP_FRAME_HEIGHT, c.height)
        if c.fps:
            rep["fps"] = self._set("fps", cv2.CAP_PROP_FPS, c.fps)
        rep["buffersize"] = self._set("buffersize", cv2.CAP_PROP_BUFFERSIZE, 1)
        modes = AUTO_EXPOSURE_VALUES.get(c.backend)
        if c.auto_exposure != "leave":
            if modes is None:
                rep["auto_exposure"] = {"requested": c.auto_exposure, "accepted": False,
                                        "note": f"no known auto-exposure convention for backend {c.backend}"}
            else:
                rep["auto_exposure"] = self._set("auto_exposure", cv2.CAP_PROP_AUTO_EXPOSURE, modes[c.auto_exposure])
        if c.exposure is not None:
            rep["exposure"] = self._set("exposure", cv2.CAP_PROP_EXPOSURE, c.exposure)
        if c.gain is not None:
            rep["gain"] = self._set("gain", cv2.CAP_PROP_GAIN, c.gain)
        if c.auto_wb != "leave":
            rep["auto_wb"] = self._set("auto_wb", cv2.CAP_PROP_AUTO_WB, 1.0 if c.auto_wb == "auto" else 0.0)
        if c.wb_temperature is not None:
            rep["wb_temperature"] = self._set("wb_temperature", cv2.CAP_PROP_WB_TEMPERATURE, c.wb_temperature)
        return rep

    def read_controls(self) -> dict:
        vals = {name: self._get(prop) for name, prop in READABLE_PROPS.items()}
        vals["fourcc_str"] = fourcc_to_str(vals["fourcc"] or 0)
        vals["backend_name"] = self.cap.getBackendName()
        # Only convert to seconds when we set the exposure ourselves and the driver accepted it;
        # a read-back alone is ambiguous (DirectShow reports -1 both for 0.5 s and for "unsupported").
        accepted = self.control_report.get("exposure", {}).get("accepted")
        vals["exposure_seconds_estimate"] = (
            exposure_seconds_estimate(self.cfg.backend, vals["exposure"]) if accepted else None)
        return vals

    def open_settings_dialog(self) -> bool:
        """Open the driver's property page (DirectShow only). Lets the operator lock exposure/WB by hand."""
        return bool(self.cap.set(cv2.CAP_PROP_SETTINGS, 1))

    # -- capture ---------------------------------------------------------
    def grab_fresh(self) -> tuple[np.ndarray | None, datetime, float, float]:
        """Discard buffered frames, then capture one.

        Uses read() for both flushing and the final capture: on DirectShow, repeated grab()
        calls faster than the frame rate report success without a new frame and the following
        retrieve() fails (observed in P003). Returns (frame or None, utc_time, monotonic_time,
        latency_ms); the time is taken as soon as the final read returns.
        """
        t0 = time.monotonic()
        for _ in range(max(0, self.cfg.flush_frames)):
            self.cap.read()
        ok, frame = self.cap.read()
        t_mono = time.monotonic()
        t_utc = datetime.now(timezone.utc)
        if not ok or frame is None or frame.size == 0:
            frame = None
        return frame, t_utc, t_mono, (t_mono - t0) * 1000.0


def list_cameras(backend: str, max_index: int = 8) -> list[dict]:
    """Try indices 0..max_index-1 and report the ones that deliver a frame."""
    found = []
    for i in range(max_index):
        cap = cv2.VideoCapture(i, BACKENDS[backend])
        try:
            if not cap.isOpened():
                continue
            ok, frame = cap.read()
            found.append({
                "index": i,
                "backend_name": cap.getBackendName(),
                "delivers_frames": bool(ok and frame is not None),
                "default_resolution": [int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)), int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))],
                "fourcc": fourcc_to_str(cap.get(cv2.CAP_PROP_FOURCC)),
            })
        finally:
            cap.release()
    return found
