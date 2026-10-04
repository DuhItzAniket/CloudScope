# CloudScope

**CloudScope is an open sky-observation system**: a professional camera application, a hardware control system for a pan-tilt sky camera, manual and autonomous operating modes, remote access, and on-device AI powered by the [STRATIA](https://github.com/DuhItzAniket/STRATIA) cloud-understanding model.

> **Status: system reboot in progress (Stage A — re-foundation).**
> The original AI-Day prototype is preserved, unchanged, in [`legacy/`](legacy/) and tagged [`v0.1-aiday`](https://github.com/DuhItzAniket/CloudScope/releases/tag/v0.1-aiday).

## What CloudScope does (target v1.0)

| Subsystem | Summary |
|---|---|
| Camera application | SharpCap-class capture: device control, live view, histogram and scopes, focus/exposure tools, sequencing, FITS / SER / TIFF / PNG / MP4 recording, session catalogue |
| Control system | Pan-tilt servos or steppers, IMU, GPS/RTC, environment sensors, through an ESP32, an Arduino Uno (lite), or Raspberry Pi 5 GPIO; calibration; safety (Sun keep-out, watchdog, e-stop) |
| Operating modes | Manual, Assisted, Autonomous (zenith cloud-base-height sampling, sky survey, time-lapse, cloud tracking, event triggers) |
| Remote access | Headless daemon, REST + WebSocket API, video streaming, web dashboard, desktop app as a remote client |
| AI | STRATIA ONNX inference with calibration-aware metadata: cloud type, layers, cover, cloud-base height with uncertainty |

Runs on Windows/Linux laptops and Raspberry Pi 5. Microcontrollers (ESP32, Arduino Uno) act as hardware controllers attached to the host.

## Repository layout

| Path | Contents |
|---|---|
| `docs/PLAN.md` | The 100-phase implementation plan (SDLC) |
| `docs/phases/` | One document per completed phase (`P###-<slug>.md`) |
| `docs/adr/` | Architecture decision records |
| `tools/sky_logger/` | Interim sky logger: start collecting sky images now ([README](tools/sky_logger/README.md)) |
| `legacy/` | Frozen v0.1 AI-Day prototype (Python training scripts, Qt/QML desktop app, ONNX models, reports) |

More folders (`core/`, `daemon/`, `apps/desktop/`, `web/`, `firmware/`, `protocol/`, `hardware/`) arrive in their phases.

## Development process

Every phase in [`docs/PLAN.md`](docs/PLAN.md) ends with a phase document, a commit `P###: <title>` carrying a `Phase-Status:` trailer, and a push. Failed or partial phases are documented, not hidden. See [`CONTRIBUTING.md`](CONTRIBUTING.md) once P002 lands.

## Related project

- **STRATIA** — the single-camera sky-understanding model CloudScope runs: https://github.com/DuhItzAniket/STRATIA
