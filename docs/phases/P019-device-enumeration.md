# P019 — Device enumeration

Status: DONE     Date: 2026-10-09     Commit: (this commit)

## Objective
Real cameras behind the HAL: a `uvc` driver that lists the computer's USB video cameras with stable ids and opens
them, on Windows through Media Foundation and on Linux through Video4Linux2.

## Requirements covered
FR-CAM-01 (camera discovery), FR-CAM-02 (stable identification), FR-CTL-01 (everything through the HAL), NFR-DATA-03
(real devices are never labelled simulated). Hardware available at this phase: the Arducam B0268 (16 MP,
`0c45:636d`) and the laptop's built-in camera (`04f2:b7b6`), both on Windows.

## Design notes
New in `core/include/cloudscope/uvc/` and `core/src/uvc/`:

| File | Content |
|---|---|
| `uvc_driver.hpp/.cpp` | `UvcDriver` (driver name `uvc`): enumeration, `create()`, `device_id()`; the `[camera]` section (`uvc = true`) |
| `uvc_camera.hpp/.cpp` | `UvcCamera`: the HAL rules (closed camera refuses, modes must be listed, controls must be offered, no mode change while streaming, buffer-size check) over a platform backend |
| `uvc_backend.hpp` (private) | `IUvcBackend`: the primitives a platform must provide; `enumerate_platform_cameras()`, `make_platform_backend()` |
| `mf_backend.cpp` (Windows) | Media Foundation: `MFEnumDeviceSources` with friendly name and symbolic link (vendor and product ids parsed from it), one asynchronous source reader per camera with a sample sink that keeps the newest three samples, native media types as modes, DirectShow `IAMCameraControl`/`IAMVideoProcAmp` as controls |
| `v4l2_backend.cpp` (Linux) | `/dev/video*` with `VIDIOC_QUERYCAP` (metadata nodes skipped), ids from sysfs, `ENUM_FMT/FRAMESIZES/FRAMEINTERVALS`, `QUERYCTRL`, memory-mapped streaming with `poll()`; compiled by CI, not yet run against hardware |

Decisions:
- **Ids are `uvc:<vid>:<pid>:<n>`**: the model's USB ids plus a count among cameras of the same model in system-path
  order. They survive restarts and re-plugging and can be stored in configuration; two cameras of one model keep
  their numbers as long as their USB ports do. The platform path (symbolic link or device node) is not in the id
  because it changes with the port and the OS.
- **Enumeration rescans every time** (hot-plug: a newly connected camera appears at the next `enumerate()`); a
  device-change notification is left to the desktop application (Stage D), which has an event loop.
- **One source reader per camera for its whole lifetime** on Windows: a media source serves one reader only (a
  second reader for streaming was refused with `Io` on the B0268 in the first attempt), and only an asynchronous
  reader can honour `read_frame()`'s timeout.
- **Real cameras are listed before the simulators** (`add_configured_drivers()` adds `uvc` first), and the UVC
  driver is on by default (`[camera] uvc = true`); tests that need the exact simulated list switch it off.

## Work log
1. Backend interface, Media Foundation backend, V4L2 backend, `UvcCamera`, `UvcDriver`, `[camera]` section (schema
   and defaults), registration in `add_configured_drivers()`.
2. `cloudscope-camtool` (`apps/camtool`): `list`, `caps`, and the measuring commands of the following phases.
3. Tests: `tests/unit/test_uvc.cpp` (ids, configuration, enumeration on this machine, hidden hardware tests:
   the HAL camera contract and a real capture); CLI tests of `cloudscope-info --devices` adapted (real cameras
   first, simulated list exact with `uvc = false`).
4. Found and fixed on the hardware: the second source reader (above); `read_frame()` with a too-small buffer must be
   refused before waiting for a sample (the contract's `InvalidArgument` rule).

## Verification
- `cloudscope-camtool list` on the development laptop:
  ```
  uvc:04f2:b7b6:1        Integrated Camera
  uvc:0c45:636d:1        Arducam_16MP
  sim:camera:sky         Simulated sky camera  [simulated]
  ```
  Both cameras are detected on Windows. Linux: the V4L2 backend compiles in CI (GCC 14/15, warnings as errors);
  detection on a Linux machine or the Raspberry Pi is verified in the Pi phase, no such machine being in the project yet.
- Hidden hardware tests against the B0268: `cloudscope-unit-tests "[hardware]"`: the full HAL camera contract
  (819 assertions: open/close rules, capabilities, every listed mode selectable, every control's range, clamping
  and automatic flag honoured, numbered time-stamped frames, buffer-size refusal) and a 10-frame capture pass.
- `ctest -C Debug`: 251 tests pass; warnings as errors on MSVC.

## Exit criteria
- [x] B0268 + laptop webcam detected: on Windows, yes (above). On Linux the backend exists and compiles; the
  detection run waits for a Linux machine (open item for the Pi phase).

## Risks / notes
- Media Foundation's exposure control is on a log2(seconds) scale; the backend converts to milliseconds and the
  read-back shows the power of two the camera uses (P021 measures it).
- The V4L2 backend is untested on hardware: treat its first run on the Pi as a hardware phase.

## Next phase
P020 — Modes.
