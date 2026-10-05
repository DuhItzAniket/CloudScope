# P017 — HAL interfaces

Status: DONE     Date: 2026-10-06     Commit: (this commit)

## Objective
Define the interfaces through which all hardware is reached (camera, mount, IMU, sensor, transport, inference engine), with a device registry and a capability model, document them, and prove with mock implementations that they can be implemented and tested.

## Requirements covered
FR-CTL-01 (all hardware behind abstract interfaces; drivers added without changing other modules), groundwork for FR-CAM-03/04/05/08 (modes, read-back, controls, time stamps), FR-CTL-11 (sensors), FR-SAF-03/05 (limits and emergency stop in the device), FR-AI-02/03 (engine per execution provider, blocking run on the caller's thread), NFR-DATA-02/03 (calibrated versus driver scale, measured versus commanded, simulated flag), NFR-USE-01 (capabilities decide what is shown).

## Design notes
New in `core/include/cloudscope/hal/` (one source file, `core/src/hal/hal.cpp`):

| Header | Content |
|---|---|
| `device.hpp` | `IDevice` (`info`, `open`, `close`, `is_open`), `DeviceInfo` (id, kind, name, driver, `simulated`), the rules common to all devices |
| `camera.hpp` | `ICamera`: capabilities (modes; controls with range, step, default, unit, automatic, calibrated), `set_mode`, `set_control` with read-back (`ControlState`), `start`/`stop`, `read_frame`; `nearest_setting()` |
| `mount.hpp` | `IMount`: capabilities (limits and maximum speed per axis, position feedback or not), `move_to`, `stop`, `emergency_stop`/`clear_emergency_stop`, `status` (position, measured or commanded, target, motion, fault, time) |
| `sensors.hpp` | `IImu` (unit quaternion sensor-to-ENU, accuracy, absolute heading or not); `ISensor` (scalar readings of ten quantities with fixed units, including GPS position and clock offset) |
| `transport.hpp` | `ITransport`: byte link, `write`, `read` with timeout |
| `inference.hpp` | `IInference`: `load`, capabilities (provider, input and output tensors), `run` on float tensors |
| `registry.hpp` | `IDriver` (`name`, `enumerate`, `create`) and `DeviceRegistry` (thread-safe; ids are `<driver>:<rest>`; `create<Interface>(id)`) |

Decisions:
- **Cameras are pulled, not pushed.** The caller's acquisition thread calls `read_frame(frame, timeout)`; the camera fills the frame it is handed (pixels, sequence number, host time stamps). A camera then has no thread of its own, one acquisition loop (P022) serves every driver, and tests read frames one at a time without threads. The draft from the previous session had each camera publish into the frame hub from its own thread; that would have repeated thread, pool and drop handling in every driver and made every camera test timing-dependent. Changed before anything depended on it. The pull form is also what vendor SDKs and V4L2 offer natively.
- **Honesty is part of the interface, not left to drivers' goodwill.** Setting a mode or control returns what is in effect (`applied` says whether the request was met); controls say whether their values are calibrated or on the driver's scale; a mount says whether its position is measured or only commanded; every device says whether it is simulated. `Timeout` ("nothing yet, still alive") and `Io` ("failed, reopen") are distinct, so the capture pipeline can tell a slow camera from a lost one.
- **The device is the last safety layer.** A mount refuses targets outside its limits and holds its emergency stop until it is cleared explicitly, also across `close()` and `open()`. The planner and safety supervisor (P057, P060) sit above this.
- **Scalar sensors share one interface** with a fixed unit per quantity, instead of one interface per sensor type: temperature, humidity, pressure, light, rain, GPS position and accuracy, and the offset of the sensor's clock (GPS, RTC) from the host clock.
- **One inference engine object per way of executing a model**, so that choosing an execution provider (FR-AI-02) is choosing between devices of the registry.
- **The rules are executable.** `tests/contract/hal_contract.hpp` has one function per interface; every implementation runs it. Mock devices (`tests/support/mock_devices.hpp`) are the first implementations; simulators (P018) and real drivers follow.
- Guide: `docs/dev/hal.md` (ideas, rules, how to write a driver, what is deliberately not in the interfaces yet).

## Work log
1. Reviewed the draft of the previous session against the requirements and the later phases that will implement the interfaces; redesigned the camera interface (pull), renamed `Quantity` to `SensorQuantity` (it collided with the unit type of `units.hpp`), added the clock-offset quantity, the mount's fault text and thread rule.
2. Wrote the seven headers, the registry and the name tables.
3. Wrote the mock devices: camera (frames filled with their sequence number; can lose frames, fall silent, be unplugged), mount (straight-line motion computed from the injected clock; fault injection), IMU, sensor, in-memory pipe, inference engine, driver.
4. Wrote the contract tests (camera, mount, IMU, sensor, transport, inference, and the rules for all devices) and ran every mock through them.
5. Wrote tests for what the mocks do beyond the contracts and for the registry (naming rules, order, kinds, typed creation, every error path, use from five threads).
6. Wrote `docs/dev/hal.md`; updated the testing guide, the style guide, the architecture document and the README.

## Verification
| Check | Result |
|---|---|
| Windows, MSVC 19.44, warnings as errors | 165/165 Debug · 166/166 Release |
| Debian 13, GCC 14.2, warnings as errors | 164/164 Debug · 165/165 Release |
| Ubuntu 26.04, GCC 15.2, warnings as errors | 164/164 Debug · 165/165 Release |
| AddressSanitizer + UndefinedBehaviorSanitizer, all tests | 164/164, no reports |
| ThreadSanitizer, all tests | 164/164, no reports |
| clang-format (64 files), clang-tidy on the new files | clean (first clang-tidy run: 12 findings in test code, all fixed in code) |

23 new tests: 6 contract runs (one per mock), 8 for mock behaviour, 9 for names, the control-rounding helper and the registry.

What the contract tests establish for every implementation that runs them:
- **All devices:** id starts with the driver's name; closed at first; open and close repeatable; reopening works.
- **Camera:** a closed camera refuses with `Unavailable`; modes are valid and listed once; the current mode is a listed one; each listed mode can be selected and is read back, an unlisted one is refused and changes nothing; for every control: effective values stay in range, out-of-range requests are clamped and reported as not applied, not-a-number is refused, "automatic" is honoured exactly where it is offered, unlisted controls are `Unsupported`; streaming: start twice and mode change while streaming are refused, frames match the mode (size, format, stride, data size) in every mode, sequence numbers rise, time stamps do not go back, a too-small buffer is refused, controls can change mid-stream, stop and close end the stream and it can be restarted.
- **Mount:** sensible limits; at rest, not in emergency stop; targets outside the limits, not-a-number and speeds outside (0, max] are refused without moving; a move reaches its target and reports `idle` there; `stop` leaves it `stopped` short of the target and a new move is accepted; an emergency stop halts it and blocks moves until cleared, also after an ordinary stop and after close and reopen; closing ends a move.
- **IMU and sensor:** unit quaternions, time in order; readings only of listed quantities, one each, finite and physically plausible.
- **Transport:** bytes arrive complete and in order in both directions; a read with nothing waiting times out with 0; small buffers get partial data; 256 KiB survive a writer and a reader on different threads; closing an end releases a waiting read.
- **Inference:** no description or run without a model; a loaded model is described; outputs match the declared names and shapes; missing, unknown or wrongly sized inputs are refused; a failed load keeps the loaded model; close unloads it.

## Exit criteria
- [x] Interfaces documented: header comments are the specification; `docs/dev/hal.md` is the guide.
- [x] Mock implementations: six mock devices and a mock driver, each passing the contract for its interface on three platforms and under the sanitizers. CI result for this commit checked after the push.

## Safety & failure-mode notes
- **The contract tests found a real fault on their first run.** The mock camera rounded an exposure request of 1000 ms (its maximum) to 1000.0000000000001 ms: stepping from the minimum in units of 0.1 does not land exactly on the maximum in floating point. A driver doing this would report an effective value outside the range it advertises. The rounding now lives in one tested function, `hal::nearest_setting()`, for mocks, simulators and drivers.
- **What the contracts cannot do:** they check behaviour that can be provoked through the interface. A driver can still be wrong about what a value means physically (an exposure reported in the wrong unit). Controls are verified against measured image brightness on real hardware in P021.
- **No contract has run against real hardware yet.** The time-dependent parts are written to work with a clock function supplied by the test; expect adjustments when the first real driver runs them (P019 for cameras, P049 onwards for the controller).
- **`stop()` and `emergency_stop()` return nothing**, so a broken link cannot be reported by them. The caller confirms through `status()`, and the controller's own heartbeat timeout (FR-SAF-04, P050/P060) is what stops a mount whose link is down. The interface must not be read as "calling emergency_stop() guarantees a stop".
- **Camera threading is a rule, not a mechanism:** one thread reads frames, one other may use the controls, and life-cycle calls must not overlap a read. The capture pipeline (P022) is the only caller and is built to that rule.
- **GCC 15 again reported what GCC 14 and MSVC did not:** a "potential null pointer dereference" inside the standard library for a vector assignment in the registry (Release build only). The code was restructured; the local Ubuntu 26.04 build caught it before the push, as intended after P016.

## Deviations & next phase
- The camera interface differs from the draft kept on the branch `wip/p017-hal` (push replaced by pull, see Design notes); that branch is obsolete.
- The architecture document named the folder `devices/`; the interfaces are in `hal/` and simulators in `sim/`. The document was updated.
- Deliberately not in the interfaces yet, each with its phase (table in `docs/dev/hal.md`): hot-plug notification (P019), raw Bayer formats, region of interest and binning (P020), controls with unevenly spaced values (P021), keep-out zone, soft limits and heartbeat towards the controller (P047, P060), homing and parking (P057), tensor types other than float.
- `PROJECT_STATE.md`: the CI entry now records the result of `9be6404` (P016 fix): all ten jobs green, coverage 94.7 % of core-library lines.
- Next: **P018 — Simulators**.
