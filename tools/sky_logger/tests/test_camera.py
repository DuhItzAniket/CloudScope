import cv2
import numpy as np
import pytest

from sky_logger import camera as camera_mod
from sky_logger.camera import Camera, CameraConfig, exposure_seconds_estimate, fourcc_to_str
from sky_logger.cli import exposure_test_verdict

MJPG = cv2.VideoWriter_fourcc(*"MJPG")


class FakeCapture:
    """Mimics the laptop webcam seen in P003: it accepts MJPG but then delivers no frames."""

    instances = []

    def __init__(self, index, backend):
        self.props = {cv2.CAP_PROP_FOURCC: float(cv2.VideoWriter_fourcc(*"YUY2")),
                      cv2.CAP_PROP_EXPOSURE: -6.0}
        self.opened = True
        FakeCapture.instances.append(self)

    def isOpened(self):
        return self.opened

    def set(self, prop, value):
        if prop == cv2.CAP_PROP_BUFFERSIZE:
            return False
        self.props[prop] = float(value)
        return True

    def get(self, prop):
        return self.props.get(prop, -1.0)

    def read(self):
        if self.props[cv2.CAP_PROP_FOURCC] == MJPG:
            return False, None
        return True, np.zeros((4, 4, 3), np.uint8)

    def grab(self):
        return self.read()[0]

    def retrieve(self):
        return self.read()

    def release(self):
        self.opened = False

    def getBackendName(self):
        return "FAKE"


@pytest.fixture
def fake_cv(monkeypatch):
    FakeCapture.instances.clear()
    monkeypatch.setattr(camera_mod.cv2, "VideoCapture", FakeCapture)
    return FakeCapture


def test_unusable_fourcc_falls_back_and_is_recorded(fake_cv):
    cam = Camera(CameraConfig(backend="dshow", fourcc="MJPG"))
    cam.open()
    rep = cam.control_report["fourcc"]
    assert "fallback" in rep and rep["readback_str"] == "YUY2"
    frame, *_ = cam.grab_fresh()
    assert frame is not None
    assert len(fake_cv.instances) == 2  # reopened once


def test_grab_fresh_survives_dshow_retrieve_quirk(fake_cv, monkeypatch):
    """DirectShow: grab() faster than the frame rate succeeds but retrieve() then fails (seen in P003)."""
    monkeypatch.setattr(FakeCapture, "retrieve", lambda self: (False, np.zeros((4, 4, 3), np.uint8)))
    cam = Camera(CameraConfig(backend="dshow", flush_frames=3))
    cam.open()
    frame, t_utc, _, latency = cam.grab_fresh()
    assert frame is not None and t_utc.tzinfo is not None and latency >= 0


def test_buffersize_rejection_is_reported_not_hidden(fake_cv):
    cam = Camera(CameraConfig(backend="dshow"))
    cam.open()
    assert cam.control_report["buffersize"]["accepted"] is False


def test_exposure_seconds_only_when_we_set_it(fake_cv):
    cam = Camera(CameraConfig(backend="dshow"))
    cam.open()
    assert cam.read_controls()["exposure_seconds_estimate"] is None   # driver value alone is ambiguous
    cam = Camera(CameraConfig(backend="dshow", exposure=-7))
    cam.open()
    assert cam.read_controls()["exposure_seconds_estimate"] == pytest.approx(2 ** -7)


def test_exposure_unit_conversions():
    assert exposure_seconds_estimate("dshow", -6) == pytest.approx(1 / 64)
    assert exposure_seconds_estimate("v4l2", 100) == pytest.approx(0.01)
    assert exposure_seconds_estimate("msmf", -6) is None
    assert fourcc_to_str(MJPG) == "MJPG" and fourcc_to_str(0) is None


@pytest.mark.parametrize("luma,repeat,expected", [
    ([40, 80, 160], 41, True),           # monotonic, reproducible
    ([136, 110, 91], 135, False),        # the P003 webcam result: brightness fell
    ([100, 101, 102], 100, False),       # no effect
    ([40, 80, 160], 90, False),          # drift: auto-exposure still running
    ([40, None, 160], 40, None),         # no frames
])
def test_exposure_verdict(luma, repeat, expected):
    ok, reason = exposure_test_verdict(luma, repeat)
    assert ok is expected and reason
