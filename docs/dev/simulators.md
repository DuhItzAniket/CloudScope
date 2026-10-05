# Simulators

CloudScope can run without any hardware: a simulated camera, pan-tilt mount, IMU, GPS receiver and set of environment sensors stand behind the same interfaces as real devices (requirement FR-CTL-02, [`hal.md`](hal.md)). Everything they deliver is labelled **simulated** in device information and in every frame (NFR-DATA-03).

Code: `core/include/cloudscope/sim/`. Tests: `tests/unit/test_sim_*.cpp`, `test_motion_profile.cpp`, `test_rotation.cpp`.

| Device id | Interface | What it simulates |
|---|---|---|
| `sim:camera:sky` | `ICamera` | A sky with drifting clouds and a Sun; exposure, gain, white balance, brightness, automatic exposure, sensor noise; 11 modes from 640 × 480 to 3840 × 2160 in BGR8, RGB8, GRAY8, GRAY16, YUYV and MJPEG |
| `sim:camera:replay` | `ICamera` | The JPEG and PNG pictures of a folder, in name order, again and again (listed only if the folder holds pictures) |
| `sim:mount:pan-tilt` | `IMount` | Two axes with limited speed and acceleration, command and telemetry latency, travel limits, optional position feedback with noise, emergency stop |
| `sim:imu:head` | `IImu` | The true orientation of the camera head with noise and, without a north reference, a drifting heading |
| `sim:sensor:gps` | `ISensor` | Position with noise after a time to first fix, accuracy, offset of GPS time from the host clock |
| `sim:sensor:environment` | `ISensor` | Temperature, humidity, pressure, light, rain |

## One rig, many devices

All devices of one `SimDriver` are views of one `SimRig`, the simulated world: the IMU turns when the mount moves, and there is one place where a test (or later a person at a simulator panel) makes things happen.

```cpp
ManualClock clock(start);                                  // or SystemClock for real time
auto driver = sim::SimDriver::make(clock, sim::SimulationConfig{}).value();
registry.add_driver(driver);
auto mount = registry.create<hal::IMount>("sim:mount:pan-tilt").value();
driver->rig()->set_axis_stalled(sim::MountAxis::Pan, true);   // what a test can make happen
```

**Time.** The mount, IMU, GPS and environment sensors have no thread and no time step: their state is a function of the clock the rig was given and is computed when asked for. With `SystemClock` they run in real time; with `ManualClock` a test decides how much time has passed, and a ten-minute scenario runs in microseconds. The camera paces its frames against real time (it sleeps until a frame is due) unless `SimulationConfig::camera.real_time` is false, in which case a read returns a frame at once: use that with `ManualClock`.

**Repeatability.** Clouds and noise derive from `SimulationConfig::seed`. Sensor noise depends on *which* sample is read, not on how often: two reads of the same sample are identical.

## Camera

- **Picture.** Clouds are drawn once on a canvas wider than the view (`make_synthetic_sky`), and the view slides over it, so the clouds drift (`cloud_drift`, fraction of the image width per second) and come back. The Sun does not drift.
- **Exposure model.** Light scales with exposure time and gain (10 ms at 0 dB shows the scene as drawn); values clip at 255. The Sun's disc is saturated at any exposure, its glow scales with exposure. White balance tilts red against blue around 5500 K. Brightness is an offset on the driver's own scale (`calibrated == false`, as on a webcam). Sensor noise is 1.5 grey levels at 0 dB and grows with gain.
- **Automatic exposure** steers the mean level to 110 a part of the way per frame, as a camera's own loop does; `control(Exposure)` reports the value it chose.
- **Timing.** Frames arrive at the mode's rate; an exposure longer than the frame period lowers the rate. A reader that falls behind finds the newest three frames buffered and has lost the older ones, which shows as a gap in `sequence`. A read with nothing due waits for its timeout and returns `Timeout`.
- **Pixel formats.** YUYV is full-range BT.601 with the colour averaged over each pixel pair; GRAY16 is the 8-bit grey value times 257; MJPEG is JPEG at quality 90.
- **Replay.** The pictures directly in the folder are shown in name order; sub-folders are not searched (for an archive sorted into folders per hour, name one of those). Pictures are delivered at the size of the first one (others are resized). In MJPEG mode a JPEG file of that size is passed on byte for byte. Frames are stamped with the current time, not the time of recording, and are labelled simulated. A file that disappears or cannot be decoded ends the stream with an error.

How fast the camera can make frames (Release build, laptop with i7-14650HX, one thread):

| Mode | Windows | Mode | Windows |
|---|---|---|---|
| 640 × 480 BGR8 | 570 fps | 1280 × 720 BGR8 | 224 fps |
| 640 × 480 GRAY16 | 380 fps | 1920 × 1080 BGR8 | 64 fps |
| 640 × 480 YUYV | 344 fps | 1920 × 1080 MJPEG | 54 fps |
| 640 × 480 MJPEG | 377 fps | 3840 × 2160 BGR8 (nominal 15 fps) | 16 fps |

Every mode is made faster than its nominal rate on this machine; on Windows the 4K mode has little margin and will lose frames on a slower one (which the sequence numbers show). Debian 13 in Docker on the same laptop: 109 fps at 1920 × 1080 and 29 fps at 3840 × 2160. Measure with `cloudscope-unit-tests "[simbench]"`.

## Mount

- Each axis follows a speed-up, cruise, slow-down profile (`AxisProfile`) within the configured maximum speed and acceleration. A move from rest is a straight line in pan and tilt and both axes arrive together; a move that replaces one in progress blends in from the present velocities.
- A command acts after `command_latency`; a status describes the state `telemetry_latency` ago and carries that time.
- Without position feedback (the default, like hobby servos) the status reports the **commanded** position and says so (`position_measured == false`). With feedback it reports the measured position with noise, and reports a fault when an axis is more than 2° from its command.
- `stop()` brakes at the acceleration limit; `emergency_stop()` halts dead and blocks moves until cleared.
- Ideal geometry: the camera looks at azimuth `pan + pan_zero_azimuth` and elevation `tilt`. `SimRig::true_position()`, `true_orientation()` and `true_pointing()` give the truth, without latency or noise, for tests to compare against.

## IMU, GPS, environment

- **IMU:** samples at `rate_hz`, counted from when the sensor was opened; a small random rotation as noise; heading drift of `heading_drift_deg_min` unless `absolute_heading` is set. It measures the *true* orientation: a stalled axis that a mount without feedback cannot see shows up as a disagreement between the IMU and the mount's report.
- **GPS:** no readings until `time_to_first_fix` after opening, or while the fix is taken away; then one solution per second around the configured site with Gaussian noise (height 1.5 times worse), the configured accuracy, and a clock offset of a few milliseconds. The default site is the centre of Bengaluru rounded to 0.01°.
- **Environment:** the values set on the rig with a little measurement noise.

## What a test can make happen

| Call on `SimRig` | Effect |
|---|---|
| `set_axis_stalled(axis, true)` | The axis stays where it is while the controller believes it follows; released, it catches up at maximum speed |
| `set_controller_reachable(false)` | Mount calls fail with `Io`, stop commands are lost, the mechanism finishes its move |
| `set_camera_connected(false)` | The stream ends with `Io`; the camera cannot be opened and is no longer listed |
| `set_camera_stalled(true)` | The camera stays connected but delivers nothing: reads return `Timeout` |
| `lose_camera_frames(n)` | The next `n` frames are lost; the sequence numbers show the gap |
| `set_gps_fix(false)` | The GPS receiver reports no position |
| `set_environment(quantity, value)` | Rain starts, the enclosure heats up, ... |

A simulated device has one user at a time, as real hardware does: a second `open()` of the same id fails with `Unavailable`.

## Mock or simulator?

Use a **mock** (`tests/support/mock_devices.hpp`) to test code that *uses* a device: it is exact, instant and scripted. Use a **simulator** when the behaviour of the device itself matters: image content and exposure response, motion over time, latency, noise. Both pass the same contract tests.

## What is not simulated

| Not simulated | Consequence | Planned with |
|---|---|---|
| The picture does not depend on where the mount points | Tracking, sky surveys, overlay accuracy and Sun keep-out cannot yet be tested on images | Camera models (P031) and mount kinematics (P056) |
| The Sun's position does not follow date, time and site | No day and night, no sunrise | Sun position in C++ (first needed in P029) |
| Recorded time of replayed pictures; sky-logger sidecars are not read | A replay cannot stand in for "the sky at that moment" | When a phase needs it (P072) |
| Heartbeat loss, keep-out zone and soft limits in the controller | The controller side of the safety system is not there yet | Controller protocol and safety (P047, P060) |
| Rolling shutter, lens distortion, vignetting, hot pixels, dark current | Calibration and correction steps have nothing to correct | Calibration frames (P026), intrinsic calibration (P031) |
| Backlash, overshoot, load-dependent speed, encoder resolution | Motion is ideal within its limits | Motion control (P057) |
| Accelerated time (a simulated day in minutes) | Long runs take real time unless driven by `ManualClock` on one thread | Sequencer and missions (P029, P075) |
| Sensor failures other than those in the table above | | As fault handling needs them (P081) |

A simulator proves that code is *consistent with our model* of a device, not that it works with the device. Every hardware phase is still verified on hardware.
