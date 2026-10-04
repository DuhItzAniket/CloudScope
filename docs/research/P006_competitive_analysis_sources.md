# P006 — Competitive analysis: verified sources and findings

| | |
|---|---|
| Phase | P006 — Competitive analysis (input to P006 design doc, ADR on INDI/ASCOM in P008, ADR-008 image formats, P027/P028 format work, Gate D in P046) |
| Compiled | 2026-10-04 |
| Method | Official websites, official manuals, GitHub READMEs / source / release API, and published specifications. Third-party pages are used only where the official source was unreachable, and are flagged. |
| Legend | **Y** = documented in the cited source. **P** = partial, limited, or only through a plugin/add-on module. **N** = not found in the official docs reviewed (absence of evidence, not proof). **UNVERIFIED** = could not be confirmed either way. "(assessment)" = analyst judgement derived from the cited facts, not a vendor claim. |

Reference tags such as [SC-cam] are Markdown reference links. They resolve to the URLs in §6.

---

## 0. Key conclusions (summary)

1. **There are two families of products, and nothing covers both.** Desktop capture workbenches (SharpCap, FireCapture, AMCap, and N.I.N.A. / Ekos for deep-sky automation) are interactive, operator-driven tools. All-sky systems (indi-allsky, AllSky) are headless Raspberry Pi/Linux services with a web UI. CloudScope's mix (a SharpCap-class workbench, an autonomous Pi node, remote access, and AI cloud analysis) is not offered by any single product reviewed (assessment).
2. **Platform gaps.** SharpCap ([SC-dl]) and N.I.N.A. ([NI-req]) are Windows-only. AllSky is Raspberry Pi OS only ([AS-gh]). FireCapture runs on Windows/macOS/Linux/Pi aarch64 but is closed-source, private-use freeware ([FC-home], [FC-lic]). KStars/Ekos (C++/Qt, GPL) is the closest technical analogue to CloudScope's stack ([KS-ekos], [KS-lic]).
3. **Cloud "AI" today is classification of cover state, not meteorology.** Examples: AllSkyAI classes such as `heavy_clouds` with a confidence value ([AS-ai]), local Keras models ([AS-localai]), simpleCloudDetect (Clear/Wisps/Mostly Cloudy/Overcast/Rain/Snow) ([ML-scd]), and a N.I.N.A. plugin that sends frames to vision LLMs ([NI-aiw]). Heuristic cover estimates also exist: star count ([AS-clear], [IA-gh]), IR sky-temperature ([AS-cloud]), red/blue ratio plus star deficit with an optical-flow nowcast ([AS-cf]). **None of the reviewed products documents WMO cloud-genus classification, calibrated cloud-base height, or calibrated uncertainty** (only classifier confidence scores).
4. **Pan-tilt sky survey with a Sun keep-out zone** was not found in any product reviewed. All-sky tools use a fixed fisheye. Astro tools slew a mount to targets.
5. **Standards to adopt:** SER v3 with the de-facto endianness caveat ([SER-v3], [SER-siril]); FITS 4.0 plus the SBFITSEXT/N.I.N.A. keyword conventions ([FITS-std], [MDL-fits], [NI-src]); AstroTIFF ([ATIFF]); ASCOM Alpaca as both client and server (REST, cross-platform, UDP discovery on 32227) ([AL-yaml], [AL-disc]); INDI client support on Linux/Pi to reuse INDI camera drivers, including `indi-libcamera` ([IN-3rd], [IN-gh]).

---

## 1. Product profiles

### 1.1 SharpCap (Robin Glover / AstroSharp Ltd)

| Aspect | Finding | Source |
|---|---|---|
| Current version | 4.1.14310.0, released 2026-07-10. 64-bit and 32-bit builds. | [SC-dl] |
| Platforms | Windows only (11/10/8.1/7 SP1). No Linux/macOS build listed. | [SC-dl] |
| Licence / price | Freemium, closed source. SharpCap Pro costs "£14 per year for a personal license". Most functionality is free. Pro can be trialled with test cameras, but saving with real cameras needs a licence. | [SC-pro], [SC-faq] |
| Cameras | Native SDKs: Altair, AstcamPan, AstroAsis, Atik, Basler, Celestron, iNova, Moravian, Player One, Point Grey, QHY, Starlight Xpress, SVBony, ZWO. Also ASCOM, DirectShow (UVC webcams), virtual/test cameras. | [SC-man] |
| Capture | Live mode and Still mode. ROI, binning (additive/averaging), colour spaces RGB24/32, MONO8/16, RAW8/16 plus compressed YUY2/I420/MJPEG. Capture limits by frame count or time. Software frame-rate limit and USB speed/turbo controls. Cooler setpoint and temperature readout. Window heater. | [SC-cam] |
| Formats | Video: AVI (8-bit), SER (8–16-bit, per-frame timestamps, Bayer info), WMV, ADV (occultation timing). Stills: PNG, FITS (metadata in file), TIFF (optional AstroTIFF metadata), JPEG. Each capture writes a "camera settings TXT" sidecar. Filename templates. | [SC-cam], [SC-feat] |
| Display & analysis | Histogram (log/linear, per-channel), mini histogram, display stretch (auto-stretch is Pro). FX menu: Highlight Over Exposed, Image Boost, Frame Stack, RGB Align, Reduce Noise, Threshold, Solar Colorization. Zoom up to 2000%. Reticules (crosshair, circle, up to 8 multi-reticules). Pixel value readout. Stellar photometry (experimental, Pro). | [SC-good], [SC-disp], [SC-info] |
| Focus aids | Contrast (edge), brightness range, Fourier detail, multi-star FWHM, single-star FWHM, Bahtinov mask. Autofocus scan (Pro). | [SC-good], [SC-pro] |
| Other tools | Live stacking (default, sigma-clip, and "maximum value (meteor detection)" modes; advanced options Pro). Polar alignment (live adjustment guidance Pro). Plate solving. Seeing monitor auto-capture (Pro). Smart Histogram / sensor analysis (Pro, some cameras only). Feature tracking, ADC alignment, collimation, flat correction. | [SC-feat], [SC-good], [SC-pro] |
| Automation | Simple sequence captures are free. Sequence Editor and planners (deep sky, solar system, mosaic) are Pro. Steps cover Camera, Cooler, Mount, Focuser, Wheel, Rotator, Switch, Repeat, and Processing. The docs say variables and conditionals were "deliberately left out". Can stop capture when an astronomical event occurs. No weather/safety steps documented. | [SC-auto] |
| Hardware model | ASCOM (COM) drivers via the ASCOM Platform for mount, focuser, filter wheel, rotator, and switch. **ASCOM Alpaca client**, including remote cameras, with auto-discovery on default port 32227 (LAN only). **INDIGO** devices reached through INDIGO's Alpaca bridge agent. Guiding integration with PHD2 / MGEN3. | [SC-hw], [SC-man] |
| Remote access | No built-in web UI or HTTP API documented. The only related item is a `/softwarerendering` flag for remote-desktop tools. | [SC-adv] |
| Extensibility | IronPython scripting console and object model (Pro). `/runscript` and `/runsequence` CLI arguments. No plugin API documented. | [SC-adv] |
| AI / cloud | None documented. | [SC-feat] |
| Weaknesses (assessment) | Windows-only and closed source. Annual Pro licence for automation and scripting. No headless or web operation. No sky-condition or cloud analysis. Sequencer lacks conditionals and safety handling. |  |

### 1.2 AMCap (Noël Danjou)

The official site `noeld.com` refused connections on 2026-10-04 (ECONNREFUSED / HTTP 429). The facts below come from the **archived official page** (Wayback 2025 snapshot) [AM-arch]. Anything newer is **UNVERIFIED**.

| Aspect | Finding | Source |
|---|---|---|
| Version | 9.23 (Build 300.6), released 2017-12-11 (latest on archived official page). A newer release is UNVERIFIED. | [AM-arch] |
| Platforms | Windows 7 SP1 or later, desktop only. "they won't work on any other platforms (e.g. Mac OS, Linux)". | [AM-arch] |
| Licence | Trial / "freeware and donationware" download. "A registration is required for daily or professional uses". Paid full version sold via MyCommerce. Price UNVERIFIED. | [AM-arch] |
| Capture | "Most Video-for-Windows and DirectShow-compatible devices". DV and MPEG-2 sources, analog tuner. Optional real-time compression with installed codecs. Containers: AVI, WMV, MP4. Still capture "by pressing a key or possibly an hardware trigger". Periodic snapshots. Settings persist between sessions. | [AM-arch] |
| Display | Configurable crosshair and OSD. Full-screen preview. Selectable renderer (VMR7/9, EVR). Hardware deinterlacing. Digital zoom and rotation (preview only). Still/video overlay. Always-on-top. | [AM-arch] |
| Automation / scripting | `stillcap.exe` console app for snapshots from scripts or the Windows Task Scheduler. | [AM-arch] |
| Still formats | BMP/JPEG/PNG are reported only by third-party download sites. **UNVERIFIED** against an official source. | [AM-3p] |
| Hardware / remote / astro | None (no mount/focuser control, no FITS/SER, no histogram). | [AM-arch] (absence) |
| Weaknesses (assessment) | Generic webcam tool with no astronomy features. DirectShow-era (no Media Foundation mentioned). Effectively unmaintained since 2017 per official page. Useful only as the "basic UVC capture" baseline. |  |

### 1.3 FireCapture (Torsten Edelmann)

| Aspect | Finding | Source |
|---|---|---|
| Version | v2.7.15 (March 2025). | [FC-home] |
| Platforms | Windows x64, macOS x64, Linux x64 (.deb / PPA / Fedora RPM), Raspberry Pi aarch64 (.deb). | [FC-home] |
| Licence | Closed-source freeware: "Permission is granted to anyone to use this software for private purpose". "Commercial use of this software is prohibited". No redistribution or modification. | [FC-lic] |
| Cameras | 15+ vendors (ZWO, QHY, Player One, SVBony, Touptek/Omegon, Altair, Basler, FLIR, LUCID, OGMA, Skyris, NexImage…) plus ASCOM. Webcams dropped since 2.5. | [FC-home], [FC-beta] |
| Capture | Planetary high-speed capture. Formats **AVI, SER, BMP, FIT, TIF**: 8-bit gives AVI/BMP/SER (+JPG), 16-bit gives FIT/TIF/SER. Optional ZIP of image sequences. Limits by time, frames, or file size. ROI (centre ROI on planet). Profiles (`*.cap`). Filter-prefixed filenames, auto subfolders. Ring buffer. Snapshot button. | [FC-help-cap], [FC-help-set], [FC-beta] |
| Metadata | Per-capture session logfile (`*.log`). Optional millisecond timestamp printed into the frame data (for occultations). Session browser / capture history. | [FC-help-set], [FC-home] |
| Display & analysis | Histogram (full log-scale or bar). AutoAlign (keeps the planet centred in the preview). "AutoHisto" gain control. Debayer. Hot-pixel and pre-processing tabs. Derotate tab. Night-mode colours. | [FC-help-set], [FC-beta], [FC-home] |
| Automation | AutoRun (N runs, delay between runs, RGB filter sequences), giving timelapse. No conditional sequencer documented. | [FC-help-set], [FC-home] |
| Hardware model | Telescope interface for planet autoguiding via ASCOM, camera ST4 port, Shoestring GPUSB, or Cookbook serial. ASCOM focuser with per-filter positions. Filter wheels via ASCOM and several native protocols. | [FC-help-set] |
| Remote access | None documented. | [FC-home] (absence) |
| Extensibility | "Plugin interface (integrate your own pre-processing filters)". "Script interface … (experimental version)". Community plugins exist (blur/edge detection, solar/lunar tools). | [FC-beta], [FC-home] |
| AI / cloud | None documented. | — |
| Weaknesses (assessment) | Closed source with commercial use prohibited. Planetary-only focus (no long-exposure all-sky workflow, no web/remote). Help pages partly dated (hosted at legacy `wonderplanets.de`). |  |

### 1.4 N.I.N.A. — Nighttime Imaging 'N' Astronomy

| Aspect | Finding | Source |
|---|---|---|
| Version | 3.2: GitHub release tag `Version-3.2` published 2025-11-27. The news page lists 3.2 under 2025-11-12 (minor discrepancy). 3.0 (2024-03-18) moved to .NET 8. | [NI-rel], [NI-news] |
| Platforms | "Windows 10 (64 bit) or later", x64 CPU, 3 GB RAM. C#/.NET/WPF. | [NI-req], [NI-gh] |
| Licence / price | Free, MPL-2.0. | [NI-gh], [NI-home] |
| Equipment | Camera, filter wheel, focuser, rotator, telescope, guider, switch, flat panel, weather, dome, safety monitor. Devices are categorised as Native (ZWO, QHY, Atik, Canon, Nikon…), ASCOM, or N.I.N.A.-internal. **ASCOM Alpaca discovery** built in since the 3.x line (Options › Equipment › ASCOM Alpaca Discovery). Weather comes from ASCOM ObservingConditions drivers. | [NI-eq], [NI-cl] |
| Sequencer | Advanced Sequencer with Sequential, Parallel, and Deep Sky Object containers, instructions, loop conditions, triggers, start/target/end areas, and templates. Conditions: Loop For Iterations / Time Span, Loop Until Time (incl. sunset/dusk/dawn/sunrise), altitude conditions, **Loop While Safe / Unsafe** (safety monitor), Moon altitude/illumination, **Sun Altitude**. Triggers: AF after #exposures / filter change / HFR increase / temperature / time; Dither; Restore Guiding; Center After Drift; Meridian Flip; Synchronize Dome. | [NI-adv], [NI-cond], [NI-trig] |
| Imaging tools | Framing assistant (sky surveys, offline DSO atlas), plate solving, autofocus, HFR star detection, auto-stretch preview, flat wizard. | [NI-home] |
| File formats & metadata | FITS, XISF, TIFF (AstroTIFF support listed by the AstroTIFF project). Rich FITS headers (see §4.2). The docs page lists `DATE-UTC`, but the source code writes `DATE-OBS` (UTC), `MJD-OBS`, `DATE-AVG`, `MJD-AVG`. | [NI-fits], [NI-src], [ATIFF] |
| Remote access | Not in core. Plugins: **Advanced API** (REST + WebSocket) ([NI-api]), Touch-N-Stars, Remote Copy, Home Assistant, Web Session History Viewer (names in the plugin manifest repo [NI-plug]). The **Alpaca** plugin exposes N.I.N.A. devices as Alpaca devices [NI-alp]. | [NI-plug], [NI-api], [NI-alp] |
| Extensibility | Plugin system with ~100+ manifests (e.g. Target Scheduler, Hocus Focus, Three Point Polar Alignment, Livestack, LuckyImaging, Python Scripting). Plugins are loaded in separate AssemblyLoadContexts since 3.2. | [NI-plug], [NI-rel], [NI-py], [NI-ls] |
| AI / cloud | Core consumes `CloudCover`/`SkyQuality` etc. from ObservingConditions and writes them to FITS (`CLOUDCVR`, `MPSAS`…). Plugins: **AI Weather** analyses all-sky images with local heuristics or cloud vision models (GPT-4o, Gemini, Claude) for cloud %, rain, and fog. It sets a safety status via a file read by the ASCOM Generic File SafetyMonitor. **Star Sentinel** stops the sequence on a sustained star-count drop. | [NI-src], [NI-aiw], [NI-ss] |
| Weaknesses (assessment) | Windows-only. Deep-sky-imaging oriented (no keogram/timelapse/all-sky products). Remote/web and live stacking depend on third-party plugins. |  |

### 1.5 KStars / Ekos + INDI (+ StellarSolver)

| Aspect | Finding | Source |
|---|---|---|
| Versions | KStars 3.8.5 (2026-10-03). INDI Library v2.2.5 (2026-10-01). StellarSolver 2.8 (2026-06-24). | [KS-home], [KS-385], [IN-rel], [SS-rel] |
| Platforms | Ekos: Windows, macOS, Linux. INDI: "MacOS, Linux, and Windows* support" (asterisk on the site) plus Raspberry Pi. The INDI repo has build instructions for Debian/Ubuntu, Arch, macOS, Windows (MSVC/MinGW). | [KS-ekos], [IN-home], [IN-gh] |
| Licence | KStars: REUSE multi-licence (GPL-2.0-or-later and others in `LICENSES/`). INDI core: LGPL-2.1 ("commercial-friendly LGPL v2"). StellarSolver: GPL-3.0. | [KS-lic], [IN-gh], [IN-home], [SS-gh] |
| Ekos modules | Capture, Focus (HFR autofocus), Guide (dithering, AO), Align (plate solving), Scheduler, Analyze. Polar alignment assistant (3 images). D-Bus scripting. 3.8.5 adds an "AI Guider" (guiding, not cloud related). | [KS-ekos], [KS-align], [KS-385] |
| Scheduler | Constraints: min altitude, Moon separation, twilight, artificial horizon, weather. Startup/shutdown procedures (unpark dome/mount…). Weather states Ok/Warning/Alert trigger a soft shutdown with a configurable grace period. `.esq` sequence files and `.esl` schedules. Greedy algorithm. Repeat N times / until terminated. | [KS-sched] |
| StellarSolver | "The Cross Platform SEP-based Star Extractor and Astrometry.net-Based Internal Astrometric Solver". C++/Qt library. No Python or temp files needed. | [SS-gh], [KS-align] |
| **INDI architecture** | XML protocol, client/server. `indiserver` launches each driver as a separate process speaking INDI on stdin/stdout and routes many-to-many between clients and drivers. Default TCP port **7624**. Remote drivers via `driver@host:port` chaining. FIFO for dynamic driver start/stop. Per-client `enableBLOB` state, with dropping of BLOBs for slow clients. | [IN-proto], [IN-server], [IN-gh] |
| INDI properties | Five vector types: Number, Switch (rules OneOfMany / AtMostOne / AnyOfMany), Text, Light, BLOB. States Idle/Ok/Busy/Alert. Client→device: `getProperties`, `enableBLOB`, `newXXXVector`. Device→client: `defXXXVector`, `setXXXVector`, `message`, `delProperty` (and `getProperties` for snooping). Updates are asynchronous. | [IN-proto] |
| INDI standard properties | `CONNECTION`, `DEVICE_PORT`, `GEOGRAPHIC_COORD`, `TIME_UTC`. CCD: `CCD_EXPOSURE`, `CCD_FRAME`, `CCD_BINNING`, `CCD_TEMPERATURE`, `CCD_COOLER`, `CCD_INFO`, `CCD_FRAME_TYPE`, `CCD_COMPRESSION`, `UPLOAD_MODE`, BLOB `CCD1` (FITS). Mount: `EQUATORIAL_EOD_COORD`, `HORIZONTAL_COORD`, `TELESCOPE_PARK`, `TELESCOPE_ABORT_MOTION`, `ON_COORD_SET`. Weather: `WEATHER_STATUS`, `WEATHER_PARAMETERS`, `SAFETY_STATUS`. | [IN-props] |
| INDI device interfaces | Camera (CCD), Telescope, Focuser, Guider, Power, Weather, Filter Wheel, Lightbox, Input, Output, Rotator, Dust Cap, Dome, PAC. GPS, Detector, AUX are listed as "documentation to be added". | [IN-ifaces] |
| Streaming / SER | `INDI::CCD` StreamManager: RAW (optional zlib) and MJPEG stream encoders. Recorders: **SER (with timestamps)** and OGV. | [IN-stream] |
| Relevant drivers | indi-3rdparty includes `indi-asi`, `indi-qhy`, `indi-playerone`, `indi-svbony`, `indi-toupbase`, `indi-sx`, **`indi-libcamera`**, `indi-webcam`, `indi-gpio`/`indi-rpi-gpio`, `indi-gpsd`, **`indi-aagcloudwatcher-ng`** (IR cloud sensor), `indi-weather-mqtt`. | [IN-3rd] |
| Remote access | Remote operation through a networked INDI server (e.g. Raspberry Pi / StellarMate). INDI Web Manager ("Web Manager for INDI Server", LGPL-2.1) for driver management. StellarMate adds mobile app, VNC, and Ekos Live (commercial hardware). | [KS-ekos], [IN-web], [SM-home] |
| Security | No authentication mechanism appears in the protocol/indiserver docs reviewed. Whether INDI offers any auth is **UNVERIFIED**. | [IN-proto], [IN-server] |
| Weaknesses (assessment) | Ekos is deep-sky oriented (no keogram/all-sky outputs). INDI on Windows is second-class (asterisk on official site). INDI's TCP/XML protocol has no documented auth, so LAN-only. StellarSolver's GPL-3.0 constrains CloudScope's licence if linked. |  |

### 1.6 indi-allsky (Aaron W. Morris)

| Aspect | Finding | Source |
|---|---|---|
| Version | Monthly releases. Latest stable `indi_v2026.09.01` (2026-09-14), nightly 2026-10-03. Bundles INDI core 2.2.4.2. arm64 (Pi 4/5) and amd64. Debian 12/13, Ubuntu 24.04/26.04. | [IA-rel] |
| Platforms / licence | Linux (Raspberry Pi OS 12/13, Debian, Ubuntu, Mint, Arch, Armbian). Python 3.9+, 1–2 GB RAM. Docker supported. GPL-3.0. | [IA-gh] |
| Camera interfaces | INDI (default; 16-bit FITS). **libcamera** via `rpicam-still`/`libcamera-still` (16-bit DNG; IMX477, IMX708, IMX500 AI Camera, Arducam 64MP…). MQTT-remote libcamera. pyCurl IP/security cameras. INDI Accumulator (synthesises long exposures from subs). INDI Passive (second instance receives images). DSLRs and webcams via INDI (`indi_v4l2_ccd`, `indi_webcam_ccd`). | [IA-camif], [IA-gh] |
| Capture & processing | Auto-gain with exposure priority. Moon mode (reduced gain when the Moon is bright and up). Cooled-camera temperature control. Dark frames (sigma-clip / average). 16-bit stretch (std-dev cutoff, MTF). Stacking (max/avg/min). Denoising. SCNR. Focus mode with variance-of-Laplacian score. | [IA-gh], [IA-rel] |
| Outputs | Real-time and long-term keograms. Star trails. ffmpeg timelapses, mini-timelapses, panorama timelapse, fisheye→panorama. Image labels (TrueType). Cardinal directions. Moon overlay. VirtualSky planetarium overlay. Circular displays. | [IA-gh], [IA-rel] |
| Detection / sky data | OpenCV star detection and count. Meteor/plane/satellite line detection (Canny + Hough) with detection masks. Satellite tracking (CelesTrak TLE). **ADS-B** aircraft tagging (SDR). Pseudo-SQM / camera SQM. Aurora/Kp (NOAA SWPC). Wildfire smoke (NA). Weather APIs (OpenWeather, WU, Astrospheric, Ambient, Ecowitt). | [IA-gh] |
| **Astrometric lens solver** | `indi_allsky/lens_solver` fits an equisolid fisheye model (VirtualSky projection, precession-corrected catalogue) to detected stars to calibrate the overlay. Present in the `indi_v2026.09.01` tag, absent in `indi_v2026.08.01`. | [IA-lens] |
| Sensors / actuators | Dozens of I²C/1-wire sensors (BME280, SHT4x, DS18B20, **MLX90614/90615 sky temperature**, MLX90640 thermal camera, TSL2591 lux, AS3935 lightning, FC-37 rain, INA2xx current…). Dew heater and fan control (GPIO/PWM/relay/MQTT). Generic GPIO. Stepper focuser. GPS (gpsd). | [IA-gh] |
| Formats & metadata | Inputs: FITS/DNG (16-bit), RGB24/PNG/JPEG. Outputs: JPEG (EXIF-tagged), PNG, WebP, TIFF. Optional FITS export (compressed FITS since 2026.08). FITS headers written in code include `DATE-OBS`, `EXPTIME`, `GAIN`, `CCD-TEMP`, `XBINNING`, `BAYERPAT`, `INSTRUME`, `TELESCOP`, `OBJECT`, `FOCALLEN`, `APTDIA`, `SITELAT`, `SITELONG`, `RA`, `DEC`. SQLite (default) or MySQL/MariaDB database of images and videos. | [IA-gh], [IA-proc], [IA-img], [IA-rel] |
| Remote access | Flask web portal (privileged areas need admin login; OIDC SSO beta). **Action API** (HTTP POST pause/unpause…). **SyncAPI** to a cloud-hosted indi-allsky. Uploads via FTP/FTPS/SFTP/WebDAV, S3 (AWS/GCS/OCI), YouTube. **MQTT** with Home Assistant auto-discovery and a Home Assistant integration. Remote INDI server operation. | [IA-gh], [IA-action] |
| AI / cloud | No built-in ML cloud classifier in the README feature list. Cloud cover is inferred indirectly (star count and brightness charts). The repo has an experimental `testing/image/mlCloudDetect.py`. Third-party **mlCloudDetect** (Keras, GPL-3.0) reads indi-allsky's DB and writes a cloudy/clear file for roof control. | [IA-gh], [IA-mlt], [ML-gt] |
| Weaknesses (assessment) | Python, Linux-only, headless (web UI only, no desktop workbench). No mount/pan-tilt control. No SER or high-speed capture. Cloud information is indirect (no cover %, genus, or height). |  |

### 1.7 AllSky (Thomas Jacquin / AllskyTeam)

| Aspect | Finding | Source |
|---|---|---|
| Version | v2026.10.01 (2026-10-01). | [AS-gh], [AS-rel] |
| Platforms / licence | Raspberry Pi Zero 2 … Pi 5, Le Potato. Pi OS "Bullseye, Bookworm or Pixie" (sic). "Non-Pi OS operating systems like Ubuntu are NOT supported". MIT licence. | [AS-gh] |
| Cameras | ZWO ("sold before August 21, 2025") plus RPi HQ / Module 3 / v1, Arducam IMX519 / 64MP / 462 / Owlsight, Waveshare IMX219-D160, OneInchEye IMX283… | [AS-gh] |
| Capture | Day and night capture. Day/night switch at a configurable **Sun altitude "Angle"** (0 = sunset, −6/−12/−18 twilight). **Auto-Exposure** with Max Auto-Exposure, **Mean Target** (0.0–1.0) and Mean Threshold. Auto-Gain. Auto white balance. ZWO histogram box for auto-exposure. Day/night gain transition. Consistent delays. Binning. Cooling (ZWO cooled). libcamera tuning files and extra parameters. Image format auto/RAW8/RGB24/RAW16. | [AS-set] |
| Formats | Output file "Supported extensions are jpg and png". JPG quality / PNG compression. Timestamped names optional on upload. **No FITS/SER/TIFF** documented. | [AS-set] |
| Processing / outputs | Resize, crop, stretch, bad-image removal, dark-frame subtraction. Timelapse (+mini). Keogram, keolapse, startrails. Overlay editor (drag-and-drop fields, fonts, images, separate day/night overlays, mask drawing tool). Chart manager. | [AS-gh] |
| Module system | Plugins ("Modules") run in five flows: Daytime, Nighttime, Day→Night, Night→Day, Periodic. Package manager, device manager (I²C database, GPIO selector). Core modules include Clear Sky (star-count threshold in an ROI), Star Count, Meteor Count, Mask, Overlay, Histogram, Export. Extra modules include ADS-B, dew heater, fans, GPS, SQM (Lux + Bortle estimate), Solar System, space weather, InfluxDB, MQTT publish, Discord, run-script. | [AS-gh], [AS-clear], [AS-mods] |
| Remote access | WebUI (login) plus public page. Remote Allsky Website or plain remote server uploads via ftps/sftp/ftp/scp/rsync/s3/gcs. Allsky Map. **Allsky Server API** (REST; JWT for remote clients; GPIO, status; dashboard and focuser "under development"). MQTT/Redis/InfluxDB/Home Assistant integrations. | [AS-gh], [AS-set], [AS-api] |
| AI / cloud (extra modules) | **AllSkyAI**: ML sky classification via allskyai.com, outputs e.g. `AI_CLASSIFICATION: heavy_clouds`, `AI_CONFIDENCE`, optional image contribution. **Local AI-Based Cloud Detection** (Keras model). **Determine Cloud Cover** (MLX90614 IR sky-minus-ambient, cloud %). **Cloud Forecast**: red/blue-ratio by day, star deficit by night, linear-trend nowcast, optional optical-flow "Cloud-Motion Nowcast" ("not a weather forecast", 30–60 min). **YOLO Rain Detector** (NCNN, lens raindrops). | [AS-ai], [AS-localai], [AS-cloud], [AS-cf], [AS-rain] |
| Weaknesses (assessment) | JPG/PNG only (no scientific formats). Pi-OS only. No hardware control beyond GPIO/dew/fan/focus stepper. Cloud features are fragmented community modules, several "Experimental". No calibrated geometry (no lens/WCS calibration found) — **UNVERIFIED** whether the `allsky_skymap` module does any. |  |

### 1.8 ASCOM Alpaca (standard)

| Aspect | Finding | Source |
|---|---|---|
| What it is | HTTP/REST + JSON device API: "ASCOM is not just for Windows any more". Targets microcontrollers (ESP32, Pi Pico), SBCs (Raspberry Pi), and PC/Mac/Linux. C# and Python (Alpyca) libraries, simulators, ConformU checker. | [AL-dev] |
| URL scheme | `http(s)://host:port/api/v1/{device_type}/{device_number}/{method}`, e.g. `/api/v1/focuser/0/position`. Common methods: `action`, `commandblind/bool/string`, `connect`, `connected`, `connecting`, `disconnect`, `devicestate`, `description`, `driverinfo`, `driverversion`, `interfaceversion`, `name`, `supportedactions`. Device API v1, OpenAPI 3.1 spec, MIT licence. | [AL-yaml] |
| Device types | camera, covercalibrator, dome, filterwheel, focuser, **observingconditions** (cloudcover, dewpoint, humidity, pressure, rainrate, skybrightness, skyquality, skytemperature, starfwhm, temperature, wind…), rotator, **safetymonitor** (`issafe`), switch, telescope (incl. `slewtoaltaz`, `slewtoaltazasync`, `synctoaltaz`, `moveaxis`, `canslewaltaz`). Camera includes `imagearray`, `gain`/`gains`, `offset`, `ccdtemperature`, `sensortype`, `bayeroffsetx/y`, `subexposureduration`, readout modes. | [AL-yaml] |
| Management & discovery | Management API: `/management/apiversions`, `/management/v1/description`, `/management/v1/configureddevices`, `/setup`. Discovery: UDP **port 32227**, probe `"alpacadiscovery1"`, reply `{"AlpacaPort": n}`; IPv6 via multicast. | [AL-mgmt], [AL-disc] |
| Image transfer | ImageBytes binary mechanic, "10 to 20 fold reduction in transfer time" vs JSON; strongly recommended for cameras. | [AL-yaml], [AL-imgbytes] |
| COM bridge | ASCOM Platform "Dynamic Clients" present an Alpaca device as a COM driver to all ASCOM clients on the PC. Discovery port configurable. Platform 7.1.3 (2026-02-18). | [AL-chooser], [AL-plat] |
| Security | "Alpaca's design purposely does not include device-level hardened security measures". Intended for "an isolated protected in-observatory local network". | [AL-dev] |
| Adoption | SharpCap (client, incl. cameras) [SC-hw]; N.I.N.A. (client discovery + plugin server) [NI-cl], [NI-alp]; simpleCloudDetect (SafetyMonitor server) [ML-scd]; INDIGO via Alpaca bridge [SC-hw]. |  |

### 1.9 Adjacent third-party cloud-detection tools (context for "AI cloud analysis")

| Tool | What it does | Source |
|---|---|---|
| mlCloudDetect (G. Tulloch) | Keras model on latest all-sky image (indi-allsky DB or any image path). Writes a cloudy/clear file with pending-state hysteresis for roof control. GPL-3.0. | [ML-gt] |
| simpleCloudDetect (chvvkumar) | Keras/Teachable-Machine classifier: Clear, Wisps, Mostly Cloudy, Overcast, Rain, Snow. MQTT + HA discovery, REST, **ASCOM Alpaca SafetyMonitor**. Docker amd64/arm64. Confidence scores. Licence not stated (UNVERIFIED). | [ML-scd] |
| N.I.N.A. AI Weather plugin | Vision-LLM or local heuristic cloud %, rain, fog. Unsafe threshold. File-based SafetyMonitor. | [NI-aiw] |
| Research: stereo cloud-base height | A network of 7 all-sky imagers (42 pairs) in Oldenburg measured CBH by stereo triangulation, better than a single pair, validated against ceilometers (Blum et al., AMT 2021). Earlier two-imager stereographic CBH: Solar Energy 2014. | [CBH-blum], [CBH-se] |

---

## 2. (a) Feature matrix

Columns: **SC** = SharpCap 4.1 · **AM** = AMCap 9.23 · **FC** = FireCapture 2.7 · **NI** = N.I.N.A. 3.2 · **EK** = KStars/Ekos 3.8.5 + INDI 2.2.5 · **IA** = indi-allsky 2026.09 · **AS** = AllSky v2026.10.01. Per-cell evidence is in the §1 profile tables. Tags point to the main source.

### 2.1 Capture

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Native astro-camera SDKs | Y (14+ vendors) [SC-man] | N | Y (15+ vendors) [FC-home] | Y (ZWO, QHY, Atik, Canon, Nikon…) [NI-eq] | Y (via INDI drivers) [IN-3rd] | Y (via INDI) [IA-camif] | P (ZWO only) [AS-gh] |
| UVC / DirectShow / V4L2 webcams | Y (DirectShow) [SC-man] | Y (DirectShow / VfW) [AM-arch] | N (dropped in 2.5) [FC-beta] | UNVERIFIED | Y (`indi-webcam`) [IN-3rd] | Y (`indi_v4l2_ccd`, `indi_webcam_ccd`) [IA-gh] | N |
| Raspberry Pi CSI (libcamera) | N (Windows only) | N | UNVERIFIED (Pi build exists; CSI support not documented) [FC-home] | N | Y (`indi-libcamera`) [IN-3rd] | Y (rpicam/libcamera, DNG) [IA-camif] | Y [AS-gh] |
| IP / network cameras | P (Alpaca remote cameras) [SC-hw] | N | N | UNVERIFIED | UNVERIFIED | Y (pyCurl) [IA-camif] | N |
| ROI / sub-frame | Y [SC-cam] | N (preview zoom only) [AM-arch] | Y [FC-beta] | UNVERIFIED | Y (`CCD_FRAME`) [IN-props] | UNVERIFIED | UNVERIFIED |
| Binning | Y [SC-cam] | N | UNVERIFIED | Y (`XBINNING`) [NI-src] | Y (`CCD_BINNING`) [IN-props] | Y (`XBINNING` header) [IA-proc] | Y [AS-set] |
| High-speed video capture | Y [SC-cam] | P (generic video) [AM-arch] | Y (planetary focus) [FC-home] | P (LuckyImaging plugin) [NI-plug] | P (INDI streaming) [IN-stream] | N | N |
| Per-frame timestamps | Y (SER/ADV) [SC-cam] | N | Y (SER + optional ms stamp in data) [FC-help-set] | Y (`DATE-OBS`/`MJD-OBS` per frame) [NI-src] | Y (SER recorder w/ timestamps) [IN-stream] | Y (`DATE-OBS`, DB) [IA-proc] | P (timestamped filenames) [AS-set] |
| Auto-exposure / auto-gain (day↔night) | UNVERIFIED | N | P ("AutoHisto" gain) [FC-beta] | N | UNVERIFIED | Y (auto-gain w/ exposure priority, Moon mode) [IA-gh] | Y (Mean Target, Max Auto-Exposure, Auto-Gain) [AS-set] |
| Cooler control | Y [SC-cam] | N | P (SensorTemp tab; cooling UNVERIFIED) [FC-help-set] | Y (`SET-TEMP`) [NI-src] | Y (`CCD_COOLER`) [IN-props] | Y [IA-gh] | Y (ZWO cooled) [AS-set] |
| Capture limits (frames/time/size) | Y (frames, time) [SC-cam] | UNVERIFIED | Y (time, frames, MB) [FC-help-cap] | Y (sequence) [NI-adv] | Y (sequence counts) [KS-sched] | n/a (continuous) | n/a (continuous) |
| Dark-frame calibration | Y (Pro) [SC-pro] | N | P (hot-pixel/pre-processing tabs) [FC-help-set] | P (Livestack plugin) [NI-ls] | UNVERIFIED | Y [IA-gh] | Y [AS-gh] |
| Capture profiles / presets | Y (camera profiles in sequencer steps) [SC-auto] | P (settings persist) [AM-arch] | Y (`*.cap` profiles) [FC-help-cap] | UNVERIFIED | UNVERIFIED | P (day/night settings) [IA-gh] | P (day/night settings) [AS-set] |

### 2.2 Display & analysis

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Live histogram | Y (log/lin, per-channel) [SC-good] | N | Y (log or bar) [FC-help-set] | UNVERIFIED | UNVERIFIED | UNVERIFIED | P (core Histogram module; content not reviewed) [AS-mods] |
| Display stretch / auto-stretch | Y (auto = Pro) [SC-disp] | N | UNVERIFIED | Y (auto-stretch preview) [NI-home] | UNVERIFIED | Y (16-bit stretch: std-dev, MTF) [IA-gh] | Y (stretch) [AS-gh] |
| Over-exposure highlight | Y (FX) [SC-disp] | N | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED | N |
| Reticules / overlays | Y (multi-reticule) [SC-info] | Y (crosshair + OSD) [AM-arch] | UNVERIFIED | UNVERIFIED | UNVERIFIED | Y (labels, cardinal dirs, Moon, VirtualSky) [IA-gh] | Y (overlay editor) [AS-gh] |
| Pixel readout / photometry | Y (photometry Pro, experimental) [SC-info] | N | UNVERIFIED | UNVERIFIED | UNVERIFIED | N | N |
| Focus aids | Y (6 metrics + autofocus Pro) [SC-good] | N | P (community plugins) [FC-home] | Y (HFR autofocus) [NI-home] | Y (HFR autofocus) [KS-ekos] | Y (variance-of-Laplacian focus mode) [IA-gh] | UNVERIFIED (focuser API "under development") [AS-api] |
| Star detection / count / HFR | Y (FWHM) [SC-good] | N | N | Y (HFR) [NI-home] | Y (SEP) [SS-gh] | Y (OpenCV count) [IA-gh] | Y (Star Count module) [AS-gh] |
| Plate solving | Y [SC-feat] | N | N | Y [NI-home] | Y (StellarSolver) [KS-align] | P (fisheye lens solver for overlay) [IA-lens] | N |
| Live stacking | Y (advanced = Pro) [SC-good] | N | N | P (Livestack plugin) [NI-ls] | UNVERIFIED | P (max/avg/min stacking) [IA-gh] | N |
| Polar alignment | Y (guidance = Pro) [SC-pro] | N | N | P (TPPA plugin) [NI-plug] | Y [KS-align] | N | N |
| Planet stabilisation / derotation | Y (Pro) [SC-pro] | N | Y (AutoAlign, Derotate) [FC-help-set] | N | N | N | N |

### 2.3 Recording & formats

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| SER | Y [SC-cam] | N | Y [FC-help-cap] | N (core) | Y (INDI SER recorder) [IN-stream] | N | N |
| AVI / MP4 / WMV | Y (AVI, WMV) [SC-cam] | Y (AVI, WMV, MP4) [AM-arch] | Y (AVI, UT-video lossless) [FC-help-set] | N | P (OGV recorder) [IN-stream] | Y (timelapse video via ffmpeg) [IA-gh] | Y (timelapse) [AS-gh] |
| FITS | Y [SC-cam] | N | Y (16-bit) [FC-help-cap] | Y [NI-fits] | Y (`CCD1` FITS BLOB) [IN-props] | Y (export, compressed) [IA-rel] | N [AS-set] |
| TIFF / AstroTIFF | Y (AstroTIFF) [SC-cam], [ATIFF] | N | Y (TIF) [FC-help-cap] | Y (AstroTIFF) [ATIFF] | UNVERIFIED | Y (TIFF) [IA-img] | N |
| PNG / JPEG | Y [SC-cam] | UNVERIFIED (3rd-party only) [AM-3p] | P (JPG 8-bit, BMP; no PNG) [FC-help-cap] | UNVERIFIED | UNVERIFIED | Y (+WebP) [IA-img] | Y (jpg, png only) [AS-set] |
| XISF | N | N | N | Y [NI-fits] | P (FITS/XISF in viewer) [KS-385] | N | N |
| Sidecar metadata / logs / DB | Y (settings TXT) [SC-cam] | N | Y (per-capture `.log`) [FC-help-set] | P (Session MetaData plugin) [NI-plug] | P (`.esq`/`.esl`, Analyze) [KS-sched] | Y (SQLite/MySQL DB, EXIF) [IA-gh] | P (module data/charts) [AS-gh] |
| Filename templates | Y [SC-feat] | UNVERIFIED | Y (filename properties) [FC-help-set] | Y (`$$…$$` variables) [NI-plug] | Y (placeholders) [KS-385] | UNVERIFIED | P (fixed name / timestamped) [AS-set] |

### 2.4 Automation

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Sequencer | Y (Pro; simple sequences free) [SC-auto] | N | P (AutoRun) [FC-help-set] | Y (Advanced Sequencer) [NI-adv] | Y [KS-sched] | N (continuous loop) | P (module flows) [AS-gh] |
| Conditions / loops / triggers | P (Repeat; "no conditionals") [SC-auto] | N | N | Y [NI-cond], [NI-trig] | P (constraints, repeats) [KS-sched] | N | N |
| Sun-altitude / twilight timing | Y ("<Astronomical Event>") [SC-auto] | N | N | Y (Sun Altitude, Loop Until dusk/dawn) [NI-cond] | Y (twilight constraint) [KS-sched] | P (Moon mode; day/night logic UNVERIFIED) [IA-gh] | Y ("Angle") [AS-set] |
| Multi-target scheduler | P (planners) [SC-auto] | N | N | P (Target Scheduler plugin) [NI-plug] | Y (greedy) [KS-sched] | N | N |
| Weather / safety-driven abort | N | N | N | Y (Loop While Safe, dome close) [NI-cond], [NI-cl] | Y (Ok/Warning/Alert soft shutdown) [KS-sched] | N | N |
| Interval / timelapse capture | P (simple sequence) [SC-auto] | Y (periodic snapshots) [AM-arch] | Y (AutoRun delay) [FC-help-set] | UNVERIFIED | UNVERIFIED | Y [IA-gh] | Y [AS-gh] |
| 24/7 unattended operation | N (assessment) | P (`stillcap.exe` + Task Scheduler) [AM-arch] | N (assessment) | P (night sessions) | P (repeat until terminated) [KS-sched] | Y [IA-gh] | Y ("24 hours a day") [AS-gh] |

### 2.5 Hardware control

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Mount / GOTO | Y (ASCOM) [SC-hw] | N | P (guide pulses only) [FC-help-set] | Y [NI-eq] | Y [IN-props] | N | N |
| Alt-az pointing (pan-tilt-like) | UNVERIFIED (via ASCOM mount) | N | N | UNVERIFIED (`CENTALT/CENTAZ` logged) [NI-src] | Y (`HORIZONTAL_COORD`) [IN-props] | N | N |
| Focuser | Y [SC-hw] | N | Y (ASCOM) [FC-help-set] | Y [NI-eq] | Y [IN-ifaces] | Y (stepper) [IA-gh] | P (stepper; API under dev) [AS-gh], [AS-api] |
| Filter wheel | Y [SC-hw] | N | Y [FC-help-set] | Y [NI-eq] | Y [IN-ifaces] | N | N |
| Rotator | Y [SC-hw] | N | N | Y [NI-eq] | Y [IN-ifaces] | N | N |
| Dome / roof | N | N | N | Y [NI-eq] | Y [IN-ifaces] | N | N |
| Switch / GPIO / power | Y (Switch) [SC-hw] | N | N | Y (Switch) [NI-eq] | Y (Power/Output, rpi-gpio) [IN-ifaces], [IN-3rd] | Y (GPIO) [IA-gh] | Y (GPIO, PWM) [AS-gh] |
| Dew heater / fan | P (camera window heater) [SC-cam] | N | N | P (via Switch; UNVERIFIED) | P (power boxes; UNVERIFIED) | Y [IA-gh] | Y [AS-gh] |
| Environmental sensors | N | N | N | Y (ObservingConditions) [NI-cl] | Y (Weather interface, AAG CloudWatcher) [IN-ifaces], [IN-3rd] | Y (dozens) [IA-gh] | Y (dozens) [AS-gh] |
| ASCOM (COM) | Y [SC-hw] | N | Y [FC-help-set] | Y [NI-eq] | UNVERIFIED | N | N |
| ASCOM Alpaca client | Y (incl. cameras) [SC-hw] | N | UNVERIFIED (possible via Platform Dynamic Clients [AL-chooser]) | Y [NI-cl] | UNVERIFIED | N | N |
| INDI client | P (INDIGO via Alpaca bridge) [SC-hw] | N | N | N | Y [KS-ekos] | Y [IA-camif] | N |
| Acts as device server | N | N | N | P (Alpaca plugin) [NI-alp] | Y (INDI drivers/server) [IN-server] | N | N |

### 2.6 Remote access

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Web UI | N | N | N | P (Touch-N-Stars plugin) [NI-plug] | P (INDI Web Manager; StellarMate app) [IN-web], [SM-home] | Y (Flask) [IA-gh] | Y (WebUI + public page) [AS-gh] |
| HTTP/REST API | N [SC-adv] | N | N | P (Advanced API plugin) [NI-api] | P (D-Bus, local) [KS-ekos] | Y (Action API, SyncAPI) [IA-action] | Y (Allsky Server API, JWT) [AS-api] |
| MQTT / Home Assistant | N | N | N | P (HA plugin) [NI-plug] | P (`indi-weather-mqtt` input) [IN-3rd] | Y (+HA discovery) [IA-gh] | Y (publish data, HA) [AS-gh] |
| Network device protocol | Y (Alpaca client) [SC-hw] | N | N | Y (Alpaca) [NI-cl] | Y (INDI TCP 7624, chaining) [IN-server] | Y (remote INDI server) [IA-gh] | N |
| Upload / cloud storage | N | N | N | P (Remote Copy plugin) [NI-plug] | UNVERIFIED | Y (FTP/SFTP/WebDAV/S3/YouTube) [IA-gh] | Y (ftps/sftp/scp/rsync/s3/gcs, Allsky Map) [AS-set] |
| Authentication | n/a | n/a | n/a | UNVERIFIED | UNVERIFIED (none found in INDI docs) | Y (admin login, OIDC beta) [IA-gh] | Y (WebUI login, JWT) [AS-gh], [AS-api] |

### 2.7 Extensibility

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Scripting | Y (IronPython, Pro) [SC-adv] | P (`stillcap.exe` CLI) [AM-arch] | P (experimental script interface) [FC-beta] | P (Python Scripting plugin) [NI-py] | Y (D-Bus) [KS-ekos] | UNVERIFIED | Y (run-script module, user buttons) [AS-gh] |
| Plugin / module system | N | N | Y (pre-processing plugins) [FC-beta] | Y (~100+ plugins) [NI-plug] | P (new INDI drivers) [IN-gh] | N | Y (modules + package manager) [AS-gh] |
| Open source | N (freemium) | N (trial/shareware) | N (private-use freeware) | Y MPL-2.0 | Y GPL / LGPL-2.1 | Y GPL-3.0 | Y MIT |

### 2.8 Sky / cloud-specific

| Feature | SC | AM | FC | NI | EK | IA | AS |
|---|---|---|---|---|---|---|---|
| Keogram | N | N | N | N | N | Y (real-time + long-term) [IA-gh] | Y (+keolapse) [AS-gh] |
| Star trails | N | N | N | N | N | Y [IA-gh] | Y [AS-gh] |
| Timelapse video | N | N | N | N | N | Y [IA-gh] | Y (+mini) [AS-gh] |
| Meteor / satellite / aircraft | P (max-value "meteor detection" stack) [SC-good] | N | N | N | N | Y (Hough lines, TLE, ADS-B) [IA-gh] | Y (meteor module, ADS-B) [AS-gh] |
| Cloud-cover estimate | N | N | N | P (from ObservingConditions; Star Sentinel; AI Weather) [NI-src], [NI-ss], [NI-aiw] | P (weather drivers e.g. AAG CloudWatcher) [IN-3rd] | P (indirect: star count / brightness) [IA-gh] | Y (Clear Sky; IR %; Cloud Forecast) [AS-clear], [AS-cloud], [AS-cf] |
| ML cloud classification | N | N | N | P (AI Weather plugin) [NI-aiw] | N | P (3rd-party mlCloudDetect) [ML-gt] | P (AllSkyAI, local Keras; extra modules) [AS-ai], [AS-localai] |
| WMO cloud genus | N | N | N | N | N | N | N |
| Cloud-base height | N | N | N | N | N | N | N |
| Uncertainty display | N | N | N | N | N | N | P (classifier `AI_CONFIDENCE` only) [AS-ai] |
| Short-term cloud nowcast | N | N | N | N | N | N | P (Cloud Forecast trend + optical flow) [AS-cf] |
| Sun/Moon ephemeris & overlays | N | N | P (planet ephemerides) [FC-home] | P (Sun/Moon conditions) [NI-cond] | Y (planetarium) [KS-home] | Y (Moon overlay, VirtualSky) [IA-gh] | Y (Solar System module) [AS-gh] |
| Fisheye astrometric calibration | N | N | N | N | N | Y (lens solver, 2026.09) [IA-lens] | UNVERIFIED |
| SQM / sky brightness | P (sensor analysis measures sky brightness) [SC-good] | N | N | P (`MPSAS` from ObservingConditions) [NI-src] | P (weather drivers) | Y (pseudo-SQM) [IA-gh] | Y (SQM module, Bortle) [AS-gh] |
| Rain / lens-drop detection | N | N | N | P (AI Weather) [NI-aiw] | P (weather drivers) | P (FC-37 rain sensor) [IA-gh] | P (YOLO rain detector) [AS-rain] |
| Pan-tilt sky survey with Sun keep-out | N | N | N | N | N | N | N |

---

## 3. (b) Must-haves, differentiators, standards

### 3.1 Must-have to be credible as a SharpCap-class capture app

These are features at least two reference products ship, and that users of the reference class will expect (assessment, grounded in §2):

1. **Camera coverage:** native SDKs for the main astro vendors (ZWO, QHY, Player One, ToupTek family, SVBony) [SC-man], [FC-home]; generic UVC (DirectShow/Media Foundation on Windows, V4L2 on Linux) [SC-man], [AM-arch]; **libcamera on Pi 5** [IA-camif], [AS-gh]. INDI/Alpaca cameras as a fallback path.
2. **Raw capture pipeline:** RAW8/RAW16/MONO16, ROI, binning, high frame rate, capture limits by frames/time/size, cooler setpoint and telemetry [SC-cam], [FC-help-cap].
3. **Formats:** SER v3 with the UTC timestamp trailer; 16-bit FITS with complete headers; TIFF with AstroTIFF; PNG/JPEG; a video container for sharing; a per-capture sidecar (settings/log); filename templates [SC-cam], [FC-help-set], [ATIFF].
4. **Live view tools:** linear/log, per-channel histogram; display stretch separate from capture data (manual + auto); over-exposure highlight; pixel-level zoom; reticules; pixel readout [SC-good], [SC-disp], [SC-info].
5. **Focus aids:** a contrast/Laplacian metric and a star HFR/FWHM metric (Bahtinov optional) [SC-good], [IA-gh].
6. **Calibration:** dark library and hot-pixel removal; flats optional [SC-pro], [IA-gh], [AS-gh].
7. **Day/night auto-exposure:** mean-target auto-exposure/auto-gain with max exposure, switching at a configurable Sun altitude [AS-set], [IA-gh].
8. **Sequencing:** interval/timelapse capture, repeat loops, astronomical-event timing (Sun altitude/twilight). Safety-driven abort is expected by N.I.N.A./Ekos users [SC-auto], [NI-cond], [KS-sched].
9. **All-sky products:** keogram, star trails, timelapse — table stakes for all-sky users [IA-gh], [AS-gh].
10. **Remote/headless:** web dashboard, REST control API, MQTT telemetry with Home Assistant discovery, file upload (SFTP/S3) [IA-gh], [AS-gh].
11. **Hardware abstraction:** mount/focuser/filter wheel/switch through a standard driver model (ASCOM/Alpaca and/or INDI) [SC-hw], [NI-eq], [IN-proto].
12. **Session metadata:** database or index of frames with exposure, gain, temperature, and site [IA-gh].

### 3.2 Differentiators (not found, or found only partially, in the reviewed products)

| Differentiator | Status in reviewed products | Notes / evidence |
|---|---|---|
| **Calibrated cloud-base height (CBH)** | Not found in any (§2.8) | Stereo CBH from all-sky imager pairs/networks is established research [CBH-blum], [CBH-se]. Hobby software has not productised it. Needs ≥2 calibrated cameras or a camera plus pan-tilt parallax/ceilometer, and accurate geometric calibration. |
| **WMO cloud-genus AI** | Not found | Existing ML outputs cover-state classes (Clear/Wisps/Mostly Cloudy/Overcast/Rain/Snow [ML-scd]; e.g. `heavy_clouds` [AS-ai]), not the 10 WMO genera [WMO-gen]. |
| **Uncertainty display** (calibrated probabilities, error bars on cover %/CBH, per-pixel confidence) | Only raw classifier confidence (`AI_CONFIDENCE` [AS-ai]; confidence scores [ML-scd]) | Differentiate by showing calibrated uncertainty and propagating it to safety decisions (assessment). |
| **Pan-tilt sky survey with Sun keep-out** | Not found | All-sky tools are fixed fisheyes; astro tools GOTO targets. Alt-az standards exist to drive it: INDI `HORIZONTAL_COORD` [IN-props], Alpaca `slewtoaltaz` [AL-yaml]. Whether KStars/Ekos has any Sun-proximity slew guard is **UNVERIFIED**. |
| **Desktop workbench and autonomous all-sky node in one codebase** | Not found | SharpCap/FireCapture/N.I.N.A. are desktop-only; indi-allsky/AllSky are headless web services (§1). |
| **Native C++/Qt on Windows and Pi 5** | KStars/Ekos is the only C++/Qt cross-platform peer [KS-ekos], [SS-gh] | SharpCap/N.I.N.A. are Windows-only; indi-allsky/AllSky are Linux/Pi-only. |
| **CloudScope as an Alpaca SafetyMonitor + ObservingConditions server** | Few (simpleCloudDetect SafetyMonitor [ML-scd]; N.I.N.A. AI Weather uses file-based monitor [NI-aiw]) | Publishing `cloudcover`, `skyquality`, `skytemperature`, `issafe` over Alpaca makes CloudScope usable directly from N.I.N.A., SharpCap, and ASCOM apps [AL-yaml]. |
| **Full WCS export for fisheye frames** | indi-allsky solves the lens for its overlay [IA-lens] but writes only `RA`/`DEC` to FITS in the code reviewed [IA-proc] | Writing zenithal-projection WCS (§4.3) would let Siril/astropy users map pixels to alt-az/RA-Dec (assessment). |
| *Not differentiators (already exist)* | Day+night cloud % and optical-flow nowcast [AS-cf]; IR sky-temperature cloud sensing [AS-cloud], [IN-3rd]; fisheye lens solving [IA-lens]; meteor/ADS-B tagging [IA-gh] | CloudScope must match or exceed these rather than claim novelty. |

### 3.3 Standards worth adopting

| Standard | Recommendation | Rationale / evidence |
|---|---|---|
| **SER v3** | Write it (8/16-bit, Bayer IDs, RGB/BGR, UTC trailer). Follow the de-facto endianness-flag interpretation and round-trip test in Siril / SER Player. | Native format of FireCapture, SharpCap, INDI recorder, Siril, PIPP, AutoStakkert!, WinJUPOS [SER-home], [IN-stream]. Endianness caveat in §4.1 [SER-siril]. |
| **FITS 4.0 + community keywords** | Primary scientific still format. Mandatory keywords plus `DATE-OBS` (UTC, ISO-8601), `TIMESYS`, `MJD-OBS`, `EXPTIME`, camera/site/weather keywords per §4.2. Use `ROWORDER`. | [FITS-std], [MDL-fits], [NI-src] |
| **AstroTIFF** | FITS-style header in the TIFF ImageDescription tag for 16-bit TIFF. | Supported by SharpCap, N.I.N.A., ASTAP, CCDCiel, Siril ≥1.0 [ATIFF] |
| **ASCOM Alpaca (client + server)** | First-choice device standard for the HAL. Client: mounts / pan-tilt (Telescope alt-az), focusers, switches, ObservingConditions. Server: expose CloudScope's Camera, SafetyMonitor, ObservingConditions. Implement discovery (UDP 32227) and ImageBytes. Keep LAN-only, or wrap with CloudScope's own authenticated remote layer. | Cross-platform REST on Windows and Pi. Used by SharpCap and N.I.N.A.; COM apps reach it via Dynamic Clients [AL-yaml], [AL-disc], [AL-chooser], [SC-hw], [NI-cl]. Spec says no hardened security [AL-dev]. |
| **INDI (client)** | Second HAL backend on Linux/Pi to reuse INDI camera, weather, and GPIO drivers (`indi-asi`, `indi-libcamera`, `indi-aagcloudwatcher-ng`…). Client library is LGPL-2.1, so dynamic linking is compatible with most licences (assessment; confirm in ADR). | indi-allsky and Ekos use it [IA-camif], [KS-ekos], [IN-3rd], [IN-gh]. |
| **INDIGO** | Reach it via its Alpaca bridge rather than a native client (as SharpCap does). | [SC-hw] |
| **MQTT + Home Assistant discovery** | Telemetry and alerts. | Both all-sky leaders do this [IA-gh], [AS-gh]. |
| **Licence hygiene** | Avoid linking GPL-3.0 libraries (StellarSolver) unless CloudScope is GPL-compatible. Do not reuse FireCapture/SharpCap code or assets. | [SS-gh], [FC-lic] |

---

## 4. (c) File-format and metadata facts

### 4.1 SER file format (version 3)

Primary source: "SER format description version 3", Heiko Wilkens (v2) and Grischa Hahn (v3 extensions), dated **2014 Feb 06** [SER-v3]; overview page [SER-home].

**Structure:** 178-byte header → image frames → optional trailer (8 bytes × FrameCount) [SER-v3].

| Offset | Size | Field | Type / content |
|---:|---:|---|---|
| 0 | 14 | FileID | ASCII `"LUCAM-RECORDER"` (fixed) |
| 14 | 4 | LuID | int32 LE, Lumenera camera ID, unused (0) |
| 18 | 4 | ColorID | int32 LE: MONO=0; BAYER_RGGB=8, GRBG=9, GBRG=10, BGGR=11; BAYER_CYYM=16, YCMY=17, YMCY=18, MYYC=19; **RGB=100, BGR=101** (v3) |
| 22 | 4 | LittleEndian | int32 LE; spec: 0 = big-endian, 1 = little-endian 16-bit data (see caveat) |
| 26 | 4 | ImageWidth | int32 LE, pixels |
| 30 | 4 | ImageHeight | int32 LE, pixels |
| 34 | 4 | PixelDepthPerPlane | int32 LE, true bit depth 1..16 |
| 38 | 4 | FrameCount | int32 LE |
| 42 | 40 | Observer | ASCII 32–126, zero-padded |
| 82 | 40 | Instrument | ASCII (camera) |
| 122 | 40 | Telescope | ASCII |
| 162 | 8 | DateTime | int64 LE, start time, **local**. If ≤ 0 it is invalid and there is no timestamp trailer |
| 170 | 8 | DateTime_UTC | int64 LE, start time in UTC |

Offsets are computed from the field sizes in [SER-v3] and sum to 178.

- **Planes and bytes per pixel:** MONO…BAYER_MYYC = 1 plane; RGB/BGR = 3 planes. Depth 1–8 gives 1 byte per plane; 9–16 gives 2 bytes per plane. RGB stored `[R][G][B]`, BGR `[B][G][R]` [SER-v3].
- **Bit alignment:** 1–8-bit data is MSB-aligned; 9–16-bit data is **LSB-aligned** (e.g. 12-bit in the low 12 bits) [SER-v3].
- **Frame layout:** frames start at byte 178. Each is W × H × BytesPerPixel, top-left pixel first. The trailer starts at `178 + FrameCount × W × H × BytesPerPixel` and holds int64 **UTC** timestamps, one per frame [SER-v3].
- **Timestamp encoding:** Microsoft/.NET `DateTime` ticks — 100 ns increments since 0001-01-01. The spec notes (after R. Behrend, Univ. Genève) that it is effectively a **62-bit** unsigned value [SER-v3].
- **Features advertised:** "unlimited file size", UT timestamps per frame, 8–16 bit per plane, mono/Bayer RGB/CMY/full RGB (v3), observer/camera/telescope info [SER-home]. FrameCount is int32, so the format caps at 2³¹−1 frames (assessment from field type).
- **Endianness caveat (important):** "several of the first programs to support SER disrespect the specification regarding the endianness flag". Siril, SER Player, and others implement it "in opposite meaning to the specification" for compatibility [SER-siril]. In practice, readers treat the field as a *big-endian* flag (0 = little-endian). CloudScope should write little-endian 16-bit data and set the field the way Siril/SER Player expect, verified by the round-trip tests planned in ADR-008. The exact value must be confirmed by test, not assumed.
- **Who supports it:** Lucam Recorder, Genika, oaCapture, Siril, FireCapture, PIPP, WinJUPOS, AutoStakkert!, AviStack, RegiStax, ImagesPlus, DeTeCt, SER Player [SER-home]; SharpCap [SC-cam]; INDI recorder [IN-stream].

### 4.2 FITS header keywords for all-sky frames

Primary standard: **FITS Standard 4.0** (approved by IAU FITS WG 2016-07-22, language-edited 2018-08-13; still the current version on the FITS support office page) [FITS-std], [FITS-page]. Community conventions: **SBIG/MaxIm DL "SBFITSEXT"** [MDL-fits]; N.I.N.A.'s writer (source code, current HEAD) [NI-src]; indi-allsky's writer [IA-proc]; HEASARC dictionary [FITS-heasarc]; Siril `ROWORDER` proposal [SIRIL-row].

| Keyword | Meaning / units / format | Defined by | Written by | CloudScope recommendation |
|---|---|---|---|---|
| `SIMPLE`, `BITPIX`, `NAXIS`, `NAXISn`, `BZERO`/`BSCALE` | Mandatory structure. Unsigned 16-bit via `BITPIX=16`, `BZERO=32768`. | [FITS-std] | N.I.N.A. `BZERO 32768` [NI-fits] | Required |
| `DATE-OBS` | ISO-8601 `CCYY-MM-DDThh:mm:ss[.sss]`. Assumed **start** of observation. UTC default for dates after 1972. | [FITS-std] | MaxIm: "Universal time at the start of the exposure" [MDL-fits]. N.I.N.A.: UTC start [NI-src]. indi-allsky: `isoformat()` (timezone **UNVERIFIED**) [IA-proc] | Required, UTC, ms precision |
| `TIMESYS` | Time scale, default `'UTC'`, "strongly recommended". | [FITS-std] | — | Write `'UTC'` |
| `MJD-OBS`, `DATE-AVG`/`MJD-AVG`, `DATE-BEG`/`DATE-END` | MJD of start; mid-point; begin/end. | [FITS-std] | N.I.N.A. writes `MJD-OBS`, `DATE-AVG`, `MJD-AVG` [NI-src] | Write `MJD-OBS` + `DATE-AVG` |
| `DATE-LOC` | Local time of exposure start (convention). | — | N.I.N.A. [NI-src] | Optional |
| `EXPTIME` (and `EXPOSURE`) | Exposure time, seconds. | HEASARC dictionary [FITS-heasarc] | MaxIm [MDL-fits], N.I.N.A. (both) [NI-src], indi-allsky [IA-proc] | Write `EXPTIME` (+`EXPOSURE` for compatibility) |
| `XPOSURE`, `TELAPSE` | Effective exposure; wall-clock elapsed (s). | [FITS-std] | — | Optional (useful for accumulated/stacked frames) |
| `GAIN` | Camera gain in native driver units (not standardised). | Convention | N.I.N.A. "Sensor gain" [NI-src], indi-allsky [IA-proc] | Write; document units |
| `EGAIN` | Electrons per ADU. | SBFITSEXT [MDL-fits] | MaxIm, N.I.N.A. [NI-src] | Write if known |
| `OFFSET` | Sensor offset/black level. | Convention | N.I.N.A. [NI-src] | Write |
| `CCD-TEMP`, `SET-TEMP` | Sensor temperature and setpoint, °C. | SBFITSEXT [MDL-fits] | MaxIm, N.I.N.A., indi-allsky (`CCD-TEMP`) | Write |
| `XBINNING`/`YBINNING`, `XPIXSZ`/`YPIXSZ` | Binning; pixel size µm (incl. binning in MaxIm). | SBFITSEXT [MDL-fits] | MaxIm, N.I.N.A., indi-allsky (binning) | Write |
| `BAYERPAT`, `XBAYROFF`/`YBAYROFF` | CFA pattern and offsets. | Convention [MDL-fits] | N.I.N.A. [NI-src], indi-allsky [IA-proc] | Write for colour sensors |
| `ROWORDER` | `'TOP-DOWN'` / `'BOTTOM-UP'` row order. | Siril proposal [SIRIL-row] | N.I.N.A. `TOP-DOWN` [NI-src]; MaxIm `TOP-DOWN` [MDL-fits] | Write (avoids flipped all-sky images) |
| `INSTRUME`, `TELESCOP`, `OBSERVER`, `OBJECT` | Camera; optics; observer; target name. | Reserved [FITS-std] | All three writers | `TELESCOP` = lens; `OBJECT` = e.g. `'All-sky'` |
| `FOCALLEN`, `APTDIA`, `FOCRATIO` | Focal length mm; aperture mm; f-ratio. | SBFITSEXT [MDL-fits] | MaxIm, N.I.N.A., indi-allsky (`FOCALLEN`, `APTDIA`) | Write (fisheye lens values) |
| `SITELAT`, `SITELONG`, `SITEELEV` | Site latitude, longitude, elevation (m). **Format differs:** SBFITSEXT uses the same sexagesimal string format as `OBJCTDEC` [MDL-fits]. N.I.N.A. writes decimal degrees with comment `[deg]` [NI-src]. indi-allsky writes float degrees, rounded in one code path [IA-proc]. | Convention | All | Write decimal degrees, East-positive longitude (document it) |
| `OBSGEO-B`, `OBSGEO-L`, `OBSGEO-H` | Geodetic latitude (deg, N+), longitude (deg, **E+**), altitude (m). Alternative: `OBSGEO-X/Y/Z` (ITRS m, preferred). | [FITS-std] | — | Also write (standard, unambiguous) |
| `RA`, `DEC` | Pointing; HEASARC allows decimal degrees or sexagesimal. For all-sky = zenith. | [FITS-heasarc] | N.I.N.A. `[deg]` [NI-src]; indi-allsky zenith RA/Dec [IA-proc], [IA-gh] | Write zenith (fixed) or pan-tilt boresight (PTZ) |
| `OBJCTRA`, `OBJCTDEC` | Target RA "HH MM SS", Dec "DD MM SS". | SBFITSEXT [MDL-fits] | MaxIm; N.I.N.A. `[H M S]`/`[D M S]` [NI-src] | Optional |
| `CENTALT`, `CENTAZ` (`OBJCTALT`, `OBJCTAZ`) | Altitude/azimuth of image centre (deg). | SBFITSEXT [MDL-fits] | N.I.N.A. `CENTALT/CENTAZ` [NI-src] | **Write for pan-tilt frames** (90/— for zenith fisheye) |
| `AIRMASS` | Air mass. | [FITS-heasarc] | N.I.N.A. [NI-src] | Optional |
| `IMAGETYP` | Frame type. **Value differs:** MaxIm `'Light Frame'` etc. [MDL-fits]; N.I.N.A. `LIGHT`, `DARK` [NI-src]. | Convention | MaxIm, N.I.N.A., indi-allsky | Use `'Light Frame'`/`'Dark Frame'` (Siril/PixInsight compatibility to be tested — **UNVERIFIED**) |
| `FILTER` | Filter name. | [FITS-heasarc] | MaxIm, N.I.N.A. | Optional (e.g. IR-cut) |
| Weather: `CLOUDCVR` (%), `AMBTEMP` (°C), `HUMIDITY` (%), `DEWPOINT` (°C), `PRESSURE` (hPa), `SKYTEMP` (°C), `SKYBRGHT` (lux), `MPSAS` (mag/arcsec²), `STARFWHM`, `WINDSPD`/`WINDGUST` (kph), `WINDDIR` (deg, 0 = N, 90 = E) | Values from ASCOM ObservingConditions (N.I.N.A. converts wind m/s→kph). | Convention | N.I.N.A. [NI-src] | **Write CloudScope's own cloud-cover/SQM/sky-temp estimates here** (plus new `HIERARCH`/custom keys for genus/CBH with uncertainty) |
| `SWCREATE` / `ORIGIN` | Creating software. | SBFITSEXT / FITS | N.I.N.A. `SWCREATE`; indi-allsky `ORIGIN` [IA-proc] | Write both |
| `EQUINOX` | Equinox of coordinates (2000.0). | [FITS-std] | N.I.N.A. [NI-src] | Write with RA/Dec |

Note: N.I.N.A.'s documentation page lists `DATE-UTC` [NI-fits], but the current source writes `DATE-OBS` (UTC) [NI-src]. Treat the source as authoritative.

### 4.3 WCS for fisheye / all-sky images

- FITS 4.0 reserves zenithal projection codes `AZP`, `SZP`, `TAN`, `STG` (stereographic), `SIN`, `ARC` (zenithal equidistant), `ZPN` (zenithal polynomial), `ZEA` (zenithal equal-area), `AIR` (Table 23) [FITS-std].
- indi-allsky's lens solver models the lens as an **equisolid** fisheye (VirtualSky projection) [IA-lens]. Equisolid (r ∝ 2 sin(z/2)) has the same radial law as WCS `ZEA`; equidistant lenses match `ARC`; real lenses with distortion fit `ZPN` polynomial terms (analyst mapping, to be confirmed against Calabretta & Greisen 2002, cited in [FITS-std] §8).
- Recommendation: after star-based lens calibration, write `CTYPE1/2 = 'RA---ZEA'/'DEC--ZEA'` (or `ZPN` with `PVi_m`) plus `CRPIX`/`CRVAL`/`CD` so standard tools can project pixels. **UNVERIFIED** whether Siril/astropy handle full-hemisphere ZPN without issues — test in P027/P028.

### 4.4 AstroTIFF

FITS-style header text (from `SIMPLE` to `END`, flexible line length, CR+LF or LF) stored in TIFF baseline tag **ImageDescription (270 / 0x010E)**. Spec v1.0 finalised 2022-06-21. Supported by SharpCap, ASTAP, N.I.N.A., CCDCiel, Siril ≥ 1.0 [ATIFF].

---

## 5. UNVERIFIED register

- AMCap: any release after 9.23 (2017); official still-image formats; price. noeld.com was unreachable on 2026-10-04 [AM-live].
- SharpCap: auto-exposure control availability; alt-az pointing through ASCOM mounts.
- FireCapture: binning control; CSI/libcamera support on the Pi build; Alpaca use via ASCOM Dynamic Clients; display aids beyond the histogram.
- N.I.N.A.: histogram panel details, ROI/sub-frame capture, PNG/JPEG output, equipment profiles (not opened in docs reviewed).
- KStars/Ekos/INDI: FITS-viewer histogram/stretch tools, live stacking, dark library, TIFF/PNG output, ASCOM/Alpaca bridging, upload features, INDI authentication, Sun-proximity slew guard.
- indi-allsky: ROI/binning controls, scripting hooks, exact day/night switching rule, `DATE-OBS` timezone.
- AllSky: ROI support, `allsky_skymap` capabilities (calibrated geometry?), histogram module content.
- simpleCloudDetect licence.
- SER: the exact `LittleEndian` value that Siril/SER Player expect for little-endian data (confirm by test).
- `IMAGETYP` value spelling expected by Siril/PixInsight.

---

## 6. Sources

All accessed 2026-10-04.

**SharpCap**

[SC-feat]: https://www.sharpcap.co.uk/sharpcap/features
[SC-pro]: https://www.sharpcap.co.uk/sharpcap/sharpcap-pro/sharpcappro
[SC-faq]: https://www.sharpcap.co.uk/sharpcap/sharpcap-pro/premium
[SC-dl]: https://www.sharpcap.co.uk/sharpcap/downloads
[SC-man]: https://docs.sharpcap.co.uk/4.1/
[SC-cam]: https://docs.sharpcap.co.uk/4.1/5_ControllingCameras.htm
[SC-good]: https://docs.sharpcap.co.uk/4.1/6_GettingGoodImages.htm
[SC-disp]: https://docs.sharpcap.co.uk/4.1/7_ControllingtheDisplayedImage.htm
[SC-info]: https://docs.sharpcap.co.uk/4.1/8_GettingInformationfromtheImage.htm
[SC-auto]: https://docs.sharpcap.co.uk/4.1/11_AutomatedImaginginSharpCap.htm
[SC-hw]: https://docs.sharpcap.co.uk/4.1/12_TelescopeHardwareControl.htm
[SC-adv]: https://docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm

- SC-feat — SharpCap features list: https://www.sharpcap.co.uk/sharpcap/features
- SC-pro — SharpCap Pro features: https://www.sharpcap.co.uk/sharpcap/sharpcap-pro/sharpcappro
- SC-faq — SharpCap Pro licence FAQ: https://www.sharpcap.co.uk/sharpcap/sharpcap-pro/premium
- SC-dl — Downloads (version, OS): https://www.sharpcap.co.uk/sharpcap/downloads
- SC-man — User manual 4.1 (TOC, supported cameras): https://docs.sharpcap.co.uk/4.1/
- SC-cam — Controlling cameras / capture formats: https://docs.sharpcap.co.uk/4.1/5_ControllingCameras.htm
- SC-good — Histogram, focus, live stacking: https://docs.sharpcap.co.uk/4.1/6_GettingGoodImages.htm
- SC-disp — Display stretch and FX: https://docs.sharpcap.co.uk/4.1/7_ControllingtheDisplayedImage.htm
- SC-info — Reticules, pixel readout, photometry: https://docs.sharpcap.co.uk/4.1/8_GettingInformationfromtheImage.htm
- SC-auto — Sequencer: https://docs.sharpcap.co.uk/4.1/11_AutomatedImaginginSharpCap.htm
- SC-hw — Hardware control, ASCOM Alpaca, Indigo: https://docs.sharpcap.co.uk/4.1/12_TelescopeHardwareControl.htm
- SC-adv — Scripting, command line: https://docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm

**AMCap**

[AM-arch]: http://web.archive.org/web/2025/http://www.noeld.com/programs.asp?cat=video
[AM-live]: https://www.noeld.com/programs.asp?cat=video
[AM-3p]: https://www.filehorse.com/download-amcap/

- AM-arch — Archived official AMCap page (Wayback, 2025 snapshot; footer "© 1995-2020"): http://web.archive.org/web/2025/http://www.noeld.com/programs.asp?cat=video
- AM-live — Live official page (unreachable on 2026-10-04): https://www.noeld.com/programs.asp?cat=video
- AM-3p — Third-party download listing (still formats; not authoritative): https://www.filehorse.com/download-amcap/

**FireCapture**

[FC-home]: http://www.firecapture.de/
[FC-lic]: http://www.firecapture.de/license.html
[FC-beta]: https://firecapture.de/beta.html
[FC-help-cap]: http://www.wonderplanets.de/FireCapture/help/capture.html
[FC-help-set]: http://www.wonderplanets.de/FireCapture/help/settings.html

- FC-home — Home/download (v2.7.15, platforms, features): http://www.firecapture.de/
- FC-lic — Licence agreement: http://www.firecapture.de/license.html
- FC-beta — v2.5 beta notes (plugins, script interface, webcams dropped): https://firecapture.de/beta.html
- FC-help-cap — Official help, Capture panel (formats, limits): http://www.wonderplanets.de/FireCapture/help/capture.html
- FC-help-set — Official help, Settings (log, AutoRun, telescope, autoguiding, filter wheels): http://www.wonderplanets.de/FireCapture/help/settings.html

**N.I.N.A.**

[NI-home]: https://nighttime-imaging.eu/
[NI-news]: https://nighttime-imaging.eu/news/
[NI-gh]: https://github.com/isbeorn/nina
[NI-rel]: https://github.com/isbeorn/nina/releases
[NI-req]: https://nighttime-imaging.eu/docs/master/site/requirements/
[NI-eq]: https://nighttime-imaging.eu/docs/master/site/tabs/equipment/equipment/
[NI-fits]: https://nighttime-imaging.eu/docs/master/site/advanced/file_formats/fits/
[NI-src]: https://github.com/isbeorn/nina/blob/HEAD/NINA.Image/FileFormat/FITS/FITSHeader.cs
[NI-adv]: https://nighttime-imaging.eu/docs/master/site/sequencer/advanced/advanced/
[NI-trig]: https://nighttime-imaging.eu/docs/master/site/sequencer/advanced/triggers/
[NI-cond]: https://nighttime-imaging.eu/docs/master/site/sequencer/advanced/conditions/
[NI-cl]: https://f002.backblazeb2.com/file/ninasetup/Betas/3.1.2.3001/RELEASE_NOTES.html
[NI-plug]: https://github.com/isbeorn/nina.plugin.manifests
[NI-aiw]: https://github.com/michelebergo/nina.plugin.aiweather
[NI-alp]: https://github.com/isbeorn/nina.plugin.alpaca
[NI-api]: https://github.com/christian-photo/ninaAPI
[NI-py]: https://github.com/isbeorn/nina.plugin.python
[NI-ls]: https://github.com/isbeorn/nina.plugin.livestack
[NI-ss]: https://github.com/michelegz/nina.plugin.starsentinel

- NI-home — Project home: https://nighttime-imaging.eu/
- NI-news — News / release dates: https://nighttime-imaging.eu/news/
- NI-gh — GitHub repo (MPL-2.0, Windows, C#/.NET/WPF): https://github.com/isbeorn/nina
- NI-rel — Releases (Version-3.2, 2025-11-27): https://github.com/isbeorn/nina/releases
- NI-req — Requirements: https://nighttime-imaging.eu/docs/master/site/requirements/
- NI-eq — Equipment tab: https://nighttime-imaging.eu/docs/master/site/tabs/equipment/equipment/
- NI-fits — FITS keyword documentation: https://nighttime-imaging.eu/docs/master/site/advanced/file_formats/fits/
- NI-src — FITS header writer source: https://github.com/isbeorn/nina/blob/HEAD/NINA.Image/FileFormat/FITS/FITSHeader.cs
- NI-adv / NI-trig / NI-cond — Advanced sequencer, triggers, conditions: https://nighttime-imaging.eu/docs/master/site/sequencer/advanced/advanced/ · …/triggers/ · …/conditions/
- NI-cl — Changelog (Alpaca discovery, .NET 8, safety monitor, weather): https://f002.backblazeb2.com/file/ninasetup/Betas/3.1.2.3001/RELEASE_NOTES.html
- NI-plug — Plugin manifest repository: https://github.com/isbeorn/nina.plugin.manifests
- NI-aiw — AI Weather plugin: https://github.com/michelebergo/nina.plugin.aiweather
- NI-alp — Alpaca server plugin: https://github.com/isbeorn/nina.plugin.alpaca
- NI-api — Advanced API plugin: https://github.com/christian-photo/ninaAPI
- NI-py — Python Scripting plugin: https://github.com/isbeorn/nina.plugin.python
- NI-ls — Livestack plugin: https://github.com/isbeorn/nina.plugin.livestack
- NI-ss — Star Sentinel plugin: https://github.com/michelegz/nina.plugin.starsentinel

**KStars / Ekos / INDI / StellarSolver**

[KS-home]: https://kstars.kde.org/
[KS-385]: https://knro.blogspot.com/2026/10/kstars-385-released.html
[KS-ekos]: https://kstars-docs.kde.org/en/user_manual/ekos.html
[KS-sched]: https://kstars-docs.kde.org/en/user_manual/ekos-scheduler.html
[KS-align]: https://kstars-docs.kde.org/en/user_manual/ekos-align.html
[KS-lic]: https://github.com/KDE/kstars/tree/master/LICENSES
[SS-gh]: https://github.com/rlancaste/stellarsolver
[SS-rel]: https://github.com/rlancaste/stellarsolver/releases
[IN-home]: https://www.indilib.org/
[IN-gh]: https://github.com/indilib/indi
[IN-rel]: https://github.com/indilib/indi/releases
[IN-proto]: https://docs.indilib.org/protocol/
[IN-props]: https://docs.indilib.org/drivers/standard-properties/
[IN-ifaces]: https://docs.indilib.org/interfaces/
[IN-server]: https://docs.indilib.org/indiserver/
[IN-stream]: https://www.indilib.org/api/classStreamManager.html
[IN-3rd]: https://github.com/indilib/indi-3rdparty
[IN-web]: https://github.com/knro/indiwebmanager
[SM-home]: https://www.stellarmate.com/

- KS-home — KStars home (3.8.5): https://kstars.kde.org/
- KS-385 — KStars 3.8.5 release notes: https://knro.blogspot.com/2026/10/kstars-385-released.html
- KS-ekos — Ekos manual: https://kstars-docs.kde.org/en/user_manual/ekos.html
- KS-sched — Ekos Scheduler: https://kstars-docs.kde.org/en/user_manual/ekos-scheduler.html
- KS-align — Ekos Align / StellarSolver / polar alignment: https://kstars-docs.kde.org/en/user_manual/ekos-align.html
- KS-lic — KStars licence files (mirror): https://github.com/KDE/kstars/tree/master/LICENSES
- SS-gh / SS-rel — StellarSolver repo and releases: https://github.com/rlancaste/stellarsolver · https://github.com/rlancaste/stellarsolver/releases
- IN-home — INDI home: https://www.indilib.org/
- IN-gh / IN-rel — INDI core repo (LGPL-2.1) and releases (v2.2.5, 2026-10-01): https://github.com/indilib/indi · https://github.com/indilib/indi/releases
- IN-proto — INDI protocol: https://docs.indilib.org/protocol/
- IN-props — Standard properties: https://docs.indilib.org/drivers/standard-properties/
- IN-ifaces — Device interfaces: https://docs.indilib.org/interfaces/
- IN-server — indiserver: https://docs.indilib.org/indiserver/
- IN-stream — StreamManager API (SER recorder): https://www.indilib.org/api/classStreamManager.html
- IN-3rd — 3rd-party drivers repo: https://github.com/indilib/indi-3rdparty
- IN-web — INDI Web Manager: https://github.com/knro/indiwebmanager
- SM-home — StellarMate: https://www.stellarmate.com/

**indi-allsky**

[IA-gh]: https://github.com/aaronwmorris/indi-allsky
[IA-rel]: https://github.com/aaronwmorris/indi-allsky/releases
[IA-camif]: https://github.com/aaronwmorris/indi-allsky/blob/main/docs/wiki/Camera-Interfaces.md
[IA-action]: https://github.com/aaronwmorris/indi-allsky/blob/main/docs/wiki/Action-API.md
[IA-lens]: https://github.com/aaronwmorris/indi-allsky/tree/main/indi_allsky/lens_solver
[IA-proc]: https://github.com/aaronwmorris/indi-allsky/blob/main/indi_allsky/processing.py
[IA-img]: https://github.com/aaronwmorris/indi-allsky/blob/main/indi_allsky/image.py
[IA-mlt]: https://github.com/aaronwmorris/indi-allsky/blob/main/testing/image/mlCloudDetect.py

- IA-gh — README (features, sensors, requirements, GPL-3.0): https://github.com/aaronwmorris/indi-allsky
- IA-rel — Releases (2026.09.01, 2026.08.01, 2026.02.01 notes): https://github.com/aaronwmorris/indi-allsky/releases
- IA-camif — Camera interfaces wiki: https://github.com/aaronwmorris/indi-allsky/blob/main/docs/wiki/Camera-Interfaces.md
- IA-action — Action API wiki: https://github.com/aaronwmorris/indi-allsky/blob/main/docs/wiki/Action-API.md
- IA-lens — Lens solver module: https://github.com/aaronwmorris/indi-allsky/tree/main/indi_allsky/lens_solver
- IA-proc — FITS header writing code: https://github.com/aaronwmorris/indi-allsky/blob/main/indi_allsky/processing.py
- IA-img — Output format code: https://github.com/aaronwmorris/indi-allsky/blob/main/indi_allsky/image.py
- IA-mlt — Experimental ML cloud test script: https://github.com/aaronwmorris/indi-allsky/blob/main/testing/image/mlCloudDetect.py

**AllSky (AllskyTeam)**

[AS-gh]: https://github.com/AllskyTeam/allsky
[AS-rel]: https://github.com/AllskyTeam/allsky/releases
[AS-set]: https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_guide/settings/allsky.html
[AS-clear]: https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_modules/core/clear_sky.html
[AS-api]: https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_api.html
[AS-mods]: https://github.com/AllskyTeam/allsky-modules
[AS-ai]: https://github.com/AllskyTeam/allsky-modules/tree/master/allsky_ai
[AS-localai]: https://github.com/AllskyTeam/allsky-modules/tree/master/allsky_localai
[AS-cloud]: https://github.com/AllskyTeam/allsky-modules/tree/master/allsky_cloud
[AS-cf]: https://github.com/AllskyTeam/allsky-modules/tree/master/allsky_cloudforecast
[AS-rain]: https://github.com/AllskyTeam/allsky-modules/tree/master/allsky_raindetector

- AS-gh — README (features, requirements, MIT): https://github.com/AllskyTeam/allsky
- AS-rel — Releases (v2026.10.01): https://github.com/AllskyTeam/allsky/releases
- AS-set — Settings reference (exposure, Angle, formats, upload): https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_guide/settings/allsky.html
- AS-clear — Clear Sky core module: https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_modules/core/clear_sky.html
- AS-api — Allsky Server API: https://github.com/AllskyTeam/allsky/blob/master/html/docs/allsky_api.html
- AS-mods — Extra modules repo: https://github.com/AllskyTeam/allsky-modules
- AS-ai / AS-localai / AS-cloud / AS-cf / AS-rain — AllSkyAI, Local AI, MLX90614 cloud cover, Cloud Forecast, YOLO rain detector modules (URLs above)

**Third-party cloud detection**

[ML-gt]: https://github.com/gordtulloch/mlCloudDetect
[ML-scd]: https://github.com/chvvkumar/simpleCloudDetect

- ML-gt — mlCloudDetect: https://github.com/gordtulloch/mlCloudDetect
- ML-scd — simpleCloudDetect: https://github.com/chvvkumar/simpleCloudDetect

**ASCOM Alpaca**

[AL-dev]: https://ascom-standards.org/AlpacaDeveloper/Index.htm
[AL-api]: https://ascom-standards.org/api/
[AL-yaml]: https://ascom-standards.org/api/AlpacaDeviceAPI_v1.yaml
[AL-mgmt]: https://ascom-standards.org/api/AlpacaManagementAPI_v1.yaml
[AL-disc]: https://github.com/DanielVanNoord/AlpacaDiscoveryTests
[AL-chooser]: https://ascom-standards.org/Help/Platform/html/e3870a2f-582a-4ab4-b37f-e9b1c37a2030.htm
[AL-imgbytes]: https://www.ascom-standards.org/Developer/AlpacaImageBytes.pdf
[AL-plat]: https://github.com/ASCOMInitiative/ASCOMPlatform/releases

- AL-dev — Alpaca developer overview (scope, security note): https://ascom-standards.org/AlpacaDeveloper/Index.htm
- AL-api — Alpaca API browser: https://ascom-standards.org/api/
- AL-yaml — Device API OpenAPI spec v1 (MIT): https://ascom-standards.org/api/AlpacaDeviceAPI_v1.yaml
- AL-mgmt — Management API spec: https://ascom-standards.org/api/AlpacaManagementAPI_v1.yaml
- AL-disc — Discovery protocol reference implementations: https://github.com/DanielVanNoord/AlpacaDiscoveryTests
- AL-chooser — ASCOM Platform "Alpaca through the Chooser" (Dynamic Clients): https://ascom-standards.org/Help/Platform/html/e3870a2f-582a-4ab4-b37f-e9b1c37a2030.htm
- AL-imgbytes — ImageBytes document (linked from spec): https://www.ascom-standards.org/Developer/AlpacaImageBytes.pdf
- AL-plat — ASCOM Platform releases (7.1.3): https://github.com/ASCOMInitiative/ASCOMPlatform/releases

**File formats and metadata**

[SER-home]: http://www.grischa-hahn.homepage.t-online.de/astro/ser/
[SER-v3]: http://www.grischa-hahn.homepage.t-online.de/astro/ser/SER%20Doc%20V3b.pdf
[SER-siril]: https://siril.readthedocs.io/en/latest/file-formats/SER.html
[FITS-std]: https://fits.gsfc.nasa.gov/standard40/fits_standard40aa-le.pdf
[FITS-page]: https://fits.gsfc.nasa.gov/fits_standard.html
[FITS-heasarc]: https://heasarc.gsfc.nasa.gov/docs/fcg/common_dict.html
[MDL-fits]: https://cdn.diffractionlimited.com/help/maximdl/FITS_File_Header_Definitions.htm
[SIRIL-row]: https://free-astro.org/index.php?title=Siril:FITS_orientation
[ATIFF]: https://astro-tiff.sourceforge.io/

- SER-home — SER format home (G. Hahn): http://www.grischa-hahn.homepage.t-online.de/astro/ser/
- SER-v3 — SER Doc V3b PDF (2014-02-06): http://www.grischa-hahn.homepage.t-online.de/astro/ser/SER%20Doc%20V3b.pdf
- SER-siril — Siril SER docs (endianness caveat): https://siril.readthedocs.io/en/latest/file-formats/SER.html
- FITS-std — FITS Standard 4.0 (PDF): https://fits.gsfc.nasa.gov/standard40/fits_standard40aa-le.pdf
- FITS-page — FITS standard status page: https://fits.gsfc.nasa.gov/fits_standard.html
- FITS-heasarc — HEASARC dictionary of commonly used keywords: https://heasarc.gsfc.nasa.gov/docs/fcg/common_dict.html
- MDL-fits — MaxIm DL FITS header definitions (SBFITSEXT): https://cdn.diffractionlimited.com/help/maximdl/FITS_File_Header_Definitions.htm
- SIRIL-row — Siril FITS orientation / ROWORDER (referenced in N.I.N.A. source): https://free-astro.org/index.php?title=Siril:FITS_orientation
- ATIFF — AstroTIFF specification: https://astro-tiff.sourceforge.io/

**Cloud science**

[CBH-blum]: https://amt.copernicus.org/articles/14/5199/2021/
[CBH-se]: https://www.sciencedirect.com/science/article/abs/pii/S0038092X14002333
[WMO-gen]: https://cloudatlas.wmo.int/en/clouds-genera.html

- CBH-blum — Blum et al. 2021, "Cloud height measurement by a network of all-sky imagers", AMT 14, 5199: https://amt.copernicus.org/articles/14/5199/2021/
- CBH-se — "Stereographic methods for cloud base height determination using two sky imagers", Solar Energy (2014): https://www.sciencedirect.com/science/article/abs/pii/S0038092X14002333
- WMO-gen — WMO International Cloud Atlas, cloud genera (the site's TLS certificate was expired on 2026-10-04; the ten genera are standard WMO classification): https://cloudatlas.wmo.int/en/clouds-genera.html
