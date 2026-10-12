# Gate D — Workbench review

Date: 2026-10-12 · Phase: P046 · Scope: Stage D (P033–P046), the desktop application on the laptop with the
Arducam B0268 and the simulated devices.

## 1. Must-have features against the SharpCap-class matrix

The reference is the must-have list of the competitive analysis (`docs/research/P006_competitive_analysis_sources.md`
§3.1) restricted to what a desktop workbench provides; the other items belong to the camera subsystem (Stage C,
done), the control system (Stage E) and remote access (Stage H) and are listed below for completeness.

| # | Must-have (P006 §3.1) | Stage D item | Status | Evidence |
|---|---|---|---|---|
| 4a | Linear / log histogram, per channel | P036 `HistogramWidget`, channels as curves, log scale toggle | Done | `ui.` tests `[stretch]`; `docs/phases/P036-histogram-stretch.md` |
| 4b | Display stretch separate from capture data, manual and automatic | P036 `StretchSettings`, LUT on the preview copy only | Done | test "applying the stretch returns a new image and leaves the original unchanged"; capture reads frames from the hub, never the preview |
| 4c | Over-exposure highlight | P036 `highlight_overexposed` (magenta, preview only) | Done | test "the over-exposure highlight paints exactly the saturated pixels of a synthetic sky" |
| 4d | Pixel-level zoom | P034 `Viewport` (1:1, up to 64x, zoom around the cursor) | Done | `ui.` tests `[viewport]`; 16 MP run in P034 |
| 4e | Reticles | P037 reticle and grid; altitude circles, compass, Sun, keep-out with a calibration | Done | `ui.` tests `[overlay]` (Sun marker within 1e-6 degrees of the solar position) |
| 4f | Pixel readout | P038 pixel inspector, loupe, region statistics | Done | `ui.` tests `[image_tools]` |
| 5 | Focus aid: contrast / Laplacian metric | P038 variance of the Laplacian with history and best value; star HFR/FWHM not built (a sky camera has no stars in focus) | Done (Laplacian) | test "a sharp edge scores higher than a blurred one" |
| 12 | Session metadata: index of frames with exposure, gain, site | P040 session browser over the P030 catalogue | Done | test "a catalogue of 100,000 frames opens at once and is browsed in pages" |
| — | Dockable panels, saved layouts, dark and night themes (FR-DSP-08) | P033 | Done | tests "the panel layout and the saved perspectives persist across restarts", "switching the theme…" |
| — | Camera controls with read-back and profiles (FR-CAM-04, FR-CAM-07) | P035 | Done | tests `[camera_panel]` |
| — | Capture plans driven from the UI (FR-SEQ-04) | P039 | Done | test `[capture_panel]` |
| — | Status bar and searchable log console (FR-DSP-09) | P042 | Done | tests `[console]`, `[status]` |
| — | Shortcuts and gamepad (FR-DSP-10) | P043 | Done | tests `[shortcuts]`, `[gamepad]` |
| — | Preferences with validated configuration (FR-PLT-03 in the UI) | P041 | Done | tests `[preferences]` |

Items of §3.1 outside the desktop stage: 1 camera coverage (Stage C: UVC on Windows and Linux; libcamera, vendor
SDKs and INDI/Alpaca cameras are later phases), 2 raw capture pipeline (FR-CAM-12, P020, S), 3 formats (Stage C),
6 calibration (P026), 7 day/night auto-exposure (P025, P029), 8 sequencing (P029), 9 all-sky products (P028),
10 remote/headless (Stage H), 11 hardware abstraction (Stages E and F).

**Must-have features in scope of Stage D: 14 of 14 done.** Not built, by decision: star HFR/FWHM focus metrics
(no point source in a sky-camera picture by day; Bahtinov and star metrics can come with the all-sky night work),
live stacking (not a must-have for a cloud camera).

## 2. UX checklist (P044)

| Check | Result |
|---|---|
| Every visible control does something; unavailable features are visibly disabled with the reason (NFR-USE-01) | Mount, sensors, AI and remote segments say "none" / "not available"; sky overlays without a calibration are not drawn and a notification says why; the mode box, stream and profile buttons are disabled without a camera |
| Every value shows its unit; times show UTC and local (NFR-USE-02) | Controls carry the device's unit or "not calibrated"; exposure in ms; the session browser shows UTC and local time (`desktop.time_display`) |
| Readable on high-DPI displays (NFR-USE-03) | `PassThrough` scale-factor policy; layouts, no fixed pixel sizes except the loupe; checked at 100 % and 150 % on the laptop |
| Error messages say what happened and what to do (NFR-USE-04) | `user_message()` adds the action for NotFound / Unavailable / Io / Timeout; the preferences name the key |
| Accessible names on every interactive control; status tips on every action | Enforced by the test "every interactive control has an accessible name and every action a status tip" |
| Strings ready for translation | Every user-visible string goes through `tr()`; `QT_NO_CAST_FROM_ASCII` is on |
| Keyboard reachable | Every frequent action has a shortcut; conflicts are reported at start |
| Night use | Red theme including the live picture; no blue or green element |

## 3. Decision

**GO** for Stage E. The workbench covers every must-have of the desktop scope with the evidence above; the open
items below do not block the control system.

## 4. Open items carried forward

- 16-megapixel smoothness is verified on the laptop's OpenGL path (P034); the Raspberry Pi 5 figure waits for the Pi.
- Screenshot references are rendered without text; a reference for text-bearing panels needs a fixed font bundled with the application (P098 packaging).
- The gamepad drives the simulated mount only; the real mount comes with Stage E (P061 adds the mount UI).
- Star-based focus metrics (HFR/FWHM) and live stacking: not planned before v1.0 unless the night use case asks.
