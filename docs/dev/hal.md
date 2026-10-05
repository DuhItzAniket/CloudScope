# Hardware abstraction layer (HAL)

All hardware is reached through six interfaces in `core/include/cloudscope/hal/` (requirement FR-CTL-01). Drivers, simulators and test doubles implement them; nothing else in CloudScope calls an operating-system or vendor device API. A new driver therefore changes no other module.

| Interface | Header | Stands for | Implementations and their phases |
|---|---|---|---|
| `ICamera` | `camera.hpp` | A camera: modes, controls, frames | Simulated camera (P018); UVC through Media Foundation and V4L2, libcamera (P019–P022) |
| `IMount` | `mount.hpp` | A pan-tilt mount: two axes, limits, emergency stop | Simulated mount (P018); CSDP controller (P049–P051); Pi 5 GPIO, ASCOM Alpaca, INDI (P055) |
| `IImu` | `sensors.hpp` | An orientation sensor | Simulated (P018); BNO085 and others through the controller (P052) |
| `ISensor` | `sensors.hpp` | Scalar sensors: environment, light, rain, GPS | Simulated (P018); controller sensors (P053); Alpaca ObservingConditions (P055) |
| `ITransport` | `transport.hpp` | A byte link to a controller | Serial, TCP, UDP (P048) |
| `IInference` | `inference.hpp` | An engine that runs a STRATIA model | ONNX Runtime, one device per execution provider (P066) |

The headers are the specification. This page explains the ideas behind them and how to add a driver.

## Rules for every device

1. A new device object is **closed**. `open()` and `close()` can be repeated without effect; `close()` ends whatever is in progress.
2. A function that needs an open device fails with `Unavailable` when it is closed (`hal::not_open()` builds that error).
3. **Nothing is silently substituted.** A request the device cannot honour is an error, or it is answered with "not applied" and the value really in effect. The user interface shows effective values, never requested ones (FR-CAM-04).
4. A device is used from one thread at a time unless its interface says otherwise (cameras, mounts and transports do; see below).
5. `DeviceInfo::simulated` is true for every device whose data is not a measurement. It travels with the data into frames, files, the UI and the API (NFR-DATA-03).

Errors use the codes of `error.hpp` with the same meaning everywhere:

| Code | Meaning for a device |
|---|---|
| `Unavailable` | Closed, busy, not streaming, emergency stop active, no model loaded |
| `InvalidArgument` | The caller asked for something outside what the device listed (mode, target, speed, tensor) |
| `Unsupported` | The device does not have this feature at all (a control it does not list) |
| `NotFound` | No such driver, device or file |
| `Timeout` | Nothing arrived in time; trying again is correct |
| `Io` | The device or link failed; it must be closed and opened again |

## Device ids and the registry

A device id is `<driver>:<rest>`, for example `sim:camera:sky` or, later, `uvc:0c45:6366:1`. Ids stay the same across restarts and re-plugging, so they can be stored in configuration and profiles.

```cpp
hal::DeviceRegistry registry;
registry.add_driver(std::make_shared<SomeDriver>());                 // once, at start-up
for (const hal::DeviceInfo& device : registry.enumerate(hal::DeviceKind::Camera)) { ... }
auto camera = registry.create<hal::ICamera>("sim:camera:sky");       // Expected<std::shared_ptr<ICamera>>
```

The registry routes by the part of the id before the first colon. `create()` returns a new, closed object each time; asking for the wrong interface (`create<IMount>` with a camera's id) is `InvalidArgument`. The registry is thread-safe, and it calls drivers without holding its lock, because enumerating can be slow.

## Capabilities: what a device offers is discovered, never assumed

| Device | Discovered at run time |
|---|---|
| Camera | The list of modes (size, pixel format, rate); the list of controls, each with range, step, default, unit, whether it has an automatic mode, and whether its values are calibrated or on the driver's own scale |
| Mount | Travel limits and maximum speed of each axis; whether positions are measured (encoders, IMU) or only commanded |
| IMU | Whether heading is absolute (referenced to north) or drifts; sample rate |
| Sensor | Which quantities it delivers |
| Inference engine | What executes the model; names and shapes of the loaded model's inputs and outputs |

Code and user interface adapt to these lists: a control that is not listed is not shown (NFR-USE-01).

## Cameras

- **Frames are pulled.** After `start()`, the caller's acquisition thread calls `read_frame(frame, timeout)` in a loop and the camera fills the frame it is handed. A camera brings no thread of its own; one acquisition loop (P022) serves every driver, and tests read frames one at a time without any thread.
- `read_frame` fills the pixel data and all of `frame.info()`: `sequence` counts from 0 at each `start()` and a gap means frames were lost; `captured` is the host time at which the frame arrived (UTC and monotonic, FR-CAM-08).
- `Timeout` means "no frame yet, the stream is alive". `Io` means the device failed or was unplugged: the stream has ended and the camera must be closed and opened again.
- **Read-back** (FR-CAM-04): `set_mode()` returns the mode in effect; `set_control()` returns a `ControlState` with the request, the effective setting read back from the device, and `applied`. An out-of-range value is clamped and reported, not refused. `hal::nearest_setting()` computes what a range and step make of a value.
- A control's value is in its `unit` only if `calibrated` is true; otherwise it is the driver's own scale and must be stored and shown as such (NFR-DATA-02).
- Threads: one thread calls `read_frame()`. While it runs, one other thread may read and set controls. `open`, `close`, `set_mode`, `start` and `stop` must not overlap with a `read_frame()` call.

## Mounts

- Positions are **actuator angles** of the pan and tilt axes in degrees. Converting to azimuth and elevation is the kinematic model's job (P056), not a driver's.
- `move_to(target, speed)` returns at once; `status()` shows progress. The speed applies to the axis with further to go, so both axes arrive together.
- A mount refuses targets outside its limits and enforces its **emergency stop** itself: after `emergency_stop()` every move is refused until `clear_emergency_stop()`, also across `close()` and `open()` (FR-SAF-05). This is the lower of the two safety layers; the host's planner and safety supervisor sit above it.
- `status().position_measured` says whether the position comes from feedback or is only where the mount was told to be (a hobby servo).
- Every function may be called from any thread: the planner commands while the safety supervisor watches.

## Sensors

- `IImu` delivers a unit quaternion that rotates sensor coordinates into local East-North-Up (ADR-012), with the sensor's own accuracy estimate.
- `ISensor` delivers scalar readings. Each `SensorQuantity` has one fixed unit, so a reading carries no unit:

| Quantity | Unit | | Quantity | Unit |
|---|---|---|---|---|
| `temperature` | degC | | `latitude`, `longitude` | deg (north and east positive, WGS 84) |
| `relative_humidity` | % | | `altitude` | m above the ellipsoid |
| `pressure` | hPa | | `horizontal_accuracy` | m (1 sigma) |
| `illuminance` | lx | | `clock_offset` | s (sensor clock minus host UTC) |
| `rain` | 0 or 1 | | | |

- A quantity that has no value at the moment is left out of `read()` (a GPS receiver without a fix returns no position); the list from `quantities()` does not change.

## Transports and inference engines

- `ITransport` moves bytes and knows nothing about messages. One thread may write while another reads; `close()` from a third thread makes a waiting `read()` return.
- `IInference` loads one model and maps named float tensors to named float tensors. It reports what the model declares; checking that against `model_card.json` is done above the HAL (P065). `run()` blocks, so callers use their own worker thread (FR-AI-03).

## Writing a driver

1. Implement the interface for your device, and `hal::IDriver` (`name()`, `enumerate()`, `create()`) for its family. The driver's name is the prefix of its ids: lower-case letters, digits, `-` and `_`.
2. Keep ids stable: derive them from something that survives a restart and a re-plug (serial number, USB path), not from an enumeration index.
3. Report honestly: list only modes and controls that work, read values back after setting them, mark uncalibrated scales, set `simulated` where it applies.
4. Run the **contract tests** on it (next section) and add tests for what only your driver does.
5. Register the driver where the application builds its registry.

## Contract tests

`tests/contract/hal_contract.hpp` holds the rules above as test functions, one per interface:

```cpp
TEST_CASE("the mock camera obeys the camera contract", "[hal][contract]")
{
    const ManualClock clock(start);
    check_camera_contract([&clock] { return std::make_shared<MockCamera>(clock); });
}
```

The factory must return a new, closed device on each call (the functions use Catch2 sections, which run the test once per section). `check_mount_contract` also takes a function that lets time pass: it advances the clock a simulated mount runs on, or waits for a real one. Every implementation runs these functions: the mocks now, the simulators in P018, real drivers in the hardware-in-the-loop tests (P063). A driver that passes can replace any other behind its interface.

The contracts check, among other things: life-cycle and error codes of a closed device; consistent capabilities; mode selection and refusal; clamping, read-back and "automatic" for every control; frame geometry, numbering and time stamps in every mode; mount limits, motion, stop, emergency stop (including across re-opening) and closing during a move; unit quaternions; one plausible reading per listed quantity; byte order and completeness across threads; tensor names, shapes and input checking.

## Mock devices

`tests/support/mock_devices.hpp` has the smallest implementations that obey the contracts: `MockCamera`, `MockMount`, `MockImu`, `MockSensor`, `PipeTransport` (two connected in-memory ends), `MockInference`, and `MockDriver` for the registry. They never wait, take an `IClock` (use `ManualClock`), and let a test cause what is hard to cause on purpose: lost frames, a silent camera, an unplugged camera, a mount fault, a sensor without a value, a closed link.

Use a **mock** to test code that uses a device (exact, instant, scripted). Use a **simulator** (P018) when the behaviour of the device itself matters: image content, motion profiles, noise, latency.

## Not in the interfaces yet

| Topic | Why not now | Phase |
|---|---|---|
| Hot-plug notification | Needs a real driver to design against; the registry can already be polled with `enumerate()` | P019 |
| Raw Bayer pixel formats, region of interest, binning | UVC cameras have none; added with the first driver that does | P020 |
| Controls with unevenly spaced values (Windows reports exposure in powers of two) | Read-back already keeps the data honest; whether the capability list should name each value is decided on real hardware | P021 |
| Keep-out zone, soft limits and heartbeat pushed to the controller | Defined together with the controller protocol | P047, P060 |
| Homing, parking | Depend on the mechanics and the protocol | P057 |
| Tensor types other than 32-bit float | stratia-contract v1 has none | with a contract change |
