# CloudScope Desktop

`cloudscope-desktop` is the workbench: the live picture of a camera with its controls, scopes, overlays and image
tools around it, a capture panel that runs the plans of the camera tool, a browser for everything recorded, and
the log console. It lives in `build/<preset>/bin/<Config>/` and reads the same `config.toml` as the other programs
([configuration](configuration.md)); its own settings are the `[desktop]` section, edited in *File > Preferences*.

```
cloudscope-desktop                                   start with the configured camera and the last layout
cloudscope-desktop --camera sim:camera:sky --start   open a camera and stream it at once
cloudscope-desktop --mode 4656x3496@10/MJPEG         with --camera: the mode to use
cloudscope-desktop --renderer raster                 software rendering (auto: OpenGL when available)
cloudscope-desktop --theme night                     the red night-vision theme
cloudscope-desktop --reset-layout                    the default panel arrangement
cloudscope-desktop --exit-after 20                   quit after 20 s and print the preview report (benchmarks)
cloudscope-desktop --config FILE                     a configuration file on top of the standard ones
```

## Panels and layout

Every panel is a dockable window: drag its title to move it beside, above or below another one, drop it on a tab
bar to stack panels, or pull it out as a floating window. *View > Panels* shows and hides panels; *View > Save
layout as…* keeps the current arrangement under a name and *View > Layouts* brings it back. The arrangement,
the window size and the named layouts are saved when the window closes and restored at the next start
(`desktop-state.ini` next to `config.toml`).

| Panel | What it does |
|---|---|
| Live view | The camera picture. Mouse wheel zooms around the cursor, dragging pans, double-click toggles 1:1 and fit; `F` fit, `1` 1:1, `+`/`-` zoom. Shift-drag selects a region of interest for the image tools. The indicator in the corner shows the camera's rate, the display rate, lost and dropped frames, decode time and the picture size; simulated cameras are marked. |
| Camera | Devices (refresh, open, close), the modes the camera lists, every control with its real range and step, an *Auto* box where the device offers it, and the value **read back from the device** after each request: when the device clamps or ignores a request, the read-back column says so in bold (`device set 10 ms (asked 500 ms)`). Profiles save the mode and the controls under a name. |
| Histogram and stretch | Luma histogram with the colour channels, linear or logarithmic, with the clipped shares at both ends; the RGB parade; the **display stretch** (levels and gamma, manual or automatic from the 0.1 % and 99.9 % percentiles) and the over-exposure highlight (magenta). The stretch changes the preview only: frames, recordings and statistics are never touched, and the panel states what is in effect. |
| Image tools | Focus aid (variance of the Laplacian of the region of interest or the whole picture, with a history and the best value), loupe around the cursor, pixel inspector (R, G, B, luma), statistics of the region (mean, spread, minimum, maximum per channel, sharpness). |
| Capture | A plan for the sequencer: single picture, burst, interval series or exposure bracket; count, interval, file format and folder; *record into a catalogued session* creates a session folder under the session root and enters every picture into the catalogue. Progress, the time to the next picture and the list of pictures written. After a camera failure the sequencer reopens the camera and continues. |
| Sessions | The frame catalogue: pictures with thumbnails, time (UTC and local), session, format, size, exposure, gain, mean, clipped share, Sun position and whether the data is simulated. Filters by session, time, format and simulated data. *Compare two* shows two selected pictures side by side; *Export…* copies a selection with its sidecars and writes `export.json` with the catalogue's SHA-256 of every picture. Rows load in pages as you scroll, so a catalogue of 100,000 pictures opens at once. |
| Console | Every log message of the program with level and text filters, following new messages as they arrive. |

The status bar shows the camera, the mount, the sensors, the AI, remote clients and the free disk space of the
session folder; what is not there says so (`Mount: none`, `AI: not available`). The last notification is on the
left; the number is the count of unread notifications, and clicking it opens the list.

## Overlays

*Overlays* draws a reticle and a grid over the picture. With an intrinsic calibration file (from
`cloudscope-camtool calibrate`, set in *Preferences > Paths*) and the declared pointing of the camera
(*Preferences > Site and pointing*), it also draws altitude circles (0°, 30°, 60°), the cardinal points, the Sun
from the site and the time, and the keep-out cone around it. Without a calibration these items are not drawn and a
notification says why.

## STRATIA (AI)

The STRATIA panel (View > Panels > STRATIA) runs the cloud model on the live frames (ADR-004). A model is a
*package*: a folder with `model_card.json` and one `.onnx` file, as STRATIA's `scripts/export_onnx.py` writes it.

- **Loading.** AI > Models… installs a package into the model store (`<session_root>/models/`), verifying the
  checksum the package declares; *Activate* loads it now and remembers it in the configuration (`ai.model_dir`),
  *Roll back* returns to the model that was active before, *Remove* deletes an inactive one. AI > Load model… loads
  a folder once without installing it.
- **Where it runs.** The engine is chosen by `ai.provider`: `auto` tries CUDA, then DirectML (Windows, any
  DirectX 12 GPU), then the CPU, and the status bar says which one is in use (`AI: stratia 1.0.0-demo on DirectML,
  42 ms`). Frames go to the model at most `ai.rate_hz` times per second; a frame that arrives while the model is
  busy replaces the one waiting and is counted as dropped, never queued, so the preview never stalls.
- **What is shown.** The panel gives the cloud genus with its probability (top three), the étages present, the
  cover in oktas with the expected percentage, the cloud-base height with its 80 % and 95 % intervals, and the
  reliability. On the live view, the masks (cloud white, Sun glare orange, invalid dark; cloud layers in colour
  with *Cloud layers* on) are drawn over the picture, with a badge giving the headline result.
- **Uncertain results.** When the model's reliability score is above the card's threshold, or the frame is a
  night frame, the result is marked **UNCERTAIN**: the badge says so and the masks are hidden unless *Show masks
  of uncertain results too* is on. An uncertain result is still logged, flagged.
- **Geometry.** With a calibration file and the site and pointing of Preferences, the model receives the ray map
  and the Sun's position for every frame; without them it receives "unknown", which it was trained to accept.
- **Timelines and logging.** Every result is written to the catalogue (`catalogue.sqlite`, table `ai_results`,
  with the session id when a capture is recording). The STRATIA timelines panel plots cover, cloud base with its
  band and the top genus over the chosen window, live or replayed from the catalogue for a session.
- **Benchmark.** Models… > Benchmark (or the panel's Benchmark…) times the selected model on every execution
  provider and offers the table as a Markdown report; `cloudscope-aibench` does the same from the command line.

Shortcuts: Ctrl+Shift+A pauses and resumes the model, M toggles the masks.

## Themes

*View > Dark theme* and *View > Night-vision theme*. The night theme shows everything, the live picture
included, in red only, so that the operator's dark adaptation survives; the stretch and the recordings are not
affected. The choice is remembered in `config.toml`.

## Shortcuts and gamepad

Every frequent action has a keyboard shortcut (*Preferences > Shortcuts* lists them; the configuration key is
`desktop.shortcuts`, Qt's spelling such as `"view.fit" = "Ctrl+Shift+F"`). A game controller (SDL2) jogs the
simulated pan-tilt mount: the left stick or the D-pad sets the direction and speed, *A* stops, *B* is the emergency
stop. Real mounts arrive with Stage E.

## Preferences

*File > Preferences* edits the `[desktop]` section and a few others (`camera.uvc`, `simulation.enabled`,
`logging.level`) and writes them to the user's `config.toml`. The whole file is validated first: an invalid value is
refused with the message that names the key, and nothing is written. Device and renderer changes take effect at
the next start.
