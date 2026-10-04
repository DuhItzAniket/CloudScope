# P009 — Hardware component facts: verified sources

| | |
|---|---|
| Phase | P009 — Hardware reference designs (feeds BOM per tier, wiring diagrams, power budget). Also input to P050 (ESP32 firmware), P055 (native HALs), P060 (safety), P094 (enclosure v2). |
| Compiled | 2026-10-04 |
| Method | Manufacturer datasheets, product briefs and official documentation, read from the PDF where possible. Third-party pages were used only when the official source was unreachable or does not exist, and those rows are flagged **(secondary)**. |
| Legend | **UNVERIFIED** = could not be confirmed from a primary source. **(assessment)** = engineering judgement or arithmetic derived from cited figures, not a vendor claim. **(secondary)** = distributor, reseller, community or encyclopedia source. Reference tags such as [B0268-DS] resolve to the URLs in §12. |

---

## 0. Key conclusions and surprises

1. **The B0268 is confirmed as a Sony IMX298, UVC, USB 2.0 camera**, but the datasheet publishes only a horizontal FOV (105°). Its "4K" marketing is not backed by any 3840×2160 mode in the frame-rate table. Uncompressed YUY2 tops out at 1024×768 at 8–10 fps, so full resolution is MJPG (lossy) only ([B0268-DS], [B0268-QSG]).
2. **The B0268 lens is strongly distorted.** EFL 2.72 mm on a 5.21 mm-wide sensor would give about 88° H if the lens were rectilinear, yet 105° H is quoted. An equidistant (f-θ) model predicts about 110° H. Plan for a fisheye/rational intrinsic calibration (assessment from [B0268-DS]).
3. **The Raspberry Pi AI Kit is discontinued.** The current parts are the AI HAT+ (13 TOPS Hailo-8L or 26 TOPS Hailo-8) and the AI HAT+ 2 (Hailo-10H, 40 TOPS INT4). PLAN.md still names the "Hailo-8L AI Kit" ([RPI-ai-doc], [RPI-aihat2]).
4. **The AI HAT+ is rated for 0–50 °C ambient only.** That is the tightest thermal limit in the Pi tier (the Pi 5 itself is rated 0–70 °C). It is a problem inside a sun-loaded box in Bengaluru, where the April mean daily maximum is 34.1 °C and the record is 39.2 °C ([AIHAT-PB], [RPI5-PB], [IMD-43295], [WIKI-blr]).
5. **Hailo models must be compiled to HEF on x86-64 Linux.** Hailo-8/8L use the DFC v3.x line, not the current v5.x line. ONNX is an accepted input, but compilation needs a calibration image set ([HMZ], [HMZ-GS], [ULTRA-hailo]).
6. **The MPU-6050 is reported Obsolete and the ICM-20948 EOL** on TDK's product-status field. The TDK pages themselves blocked automated fetches, so this comes via a secondary source. The ICM-20948 also has a **1.71–1.95 V VDDIO**, so it is not a 3.3 V I/O part ([OBS-brief], [ICM20948-DS]).
7. **An Uno R3 cannot run the BNO08x library** (Adafruit says Uno/Leonardo "will not work due to minimal RAM"). The only BNO08x path on an Uno is UART-RVC mode at 115200 baud ([ADA-BNO085], [BNO08x-DS]).
8. **The original ESP32-WROOM-32 is NRND.** Use the WROOM-32E or an S3 module. The ESP32-S3-WROOM-1 **R8/R16V (octal-PSRAM) variants are rated to only 65 °C ambient**, or 85 °C with PSRAM ECC, which costs 1/16 of the PSRAM ([WROOM32-NRND], [S3WROOM1-DS]).
9. **The TowerPro MG996R rotates 0–159° only** (manufacturer reply on the official page). It is rated **0–55 °C** and stalls at 1.4 A. With 159° of pan it cannot cover a full azimuth, even with a tilt flip-over ([TP-MG996R]).
10. **The PCA9685 at 50 Hz quantises to 4.88 µs per count.** That is coarser than a 1–3 µs servo deadband. The PCA9685 oscillator is only specified as "25 MHz typical", so each board needs a calibration ([PCA9685-DS], assessment).
11. **The Pi 5 drops USB to 600 mA total without a 5 V/5 A PD source.** The B0268 alone (≤200 mA) fits, but the camera plus a USB HDD/SSD needs the 5 A supply, or `PSU_MAX_CURRENT=5000` with a known 5 A non-PD source ([RPI-PD-WP], [B0268-DS]).
12. **The `pps-gpio` overlay defaults to GPIO18**, which is also hardware PWM channel PWM0[2] on the Pi 5. Assign PPS to a different pin if GPIO18 is used for a servo ([OVL-README], [RP1-DS]).

---

## 1. Arducam B0268 (USB camera)

| Item | Verified value | Source |
|---|---|---|
| Datasheet title | "IMX298 USB2.0 Wide Angle Camera Module", SKU B0268, datasheet V1.0 dated 26.08.2020 | [B0268-DS] |
| Retail name | "Arducam 16MP Wide Angle USB Camera for Laptop, 1/2.8" CMOS IMX298 Mini UVC USB2.0 4K Video Webcam…" (titles vary between "with" and "without Microphone") | [ARD-prod] (secondary: page blocked by Cloudflare, title from search index) |
| Windows device name | "Arducam IMX298 Camera" | [B0268-QSG] |
| Sensor | 1/2.8" Sony IMX298, 16 MP, max 4656 (H) × 3496 (V), pixel 1.12 µm × 1.12 µm | [B0268-DS] |
| Interface | USB 2.0, UVC-compliant. Native UVC drivers on Windows, Linux and Mac ("does not require extra drivers"). Android works with a UVC app. | [B0268-QSG], [B0268-DS] |
| MJPG modes | 10 fps @ 4656×3496 / 3264×2448 / 2592×1944. 30 fps @ 1080p / 720p / 1024×768. | [B0268-DS], [B0268-QSG] |
| YUY2 modes | **Datasheet:** 8 fps @ 1024×768, 10 fps @ "640x768". **Quick-start guide:** 10 fps @ 1024×768. "640x768" is probably a typo for 640×480 (the datasheet's AMCap screenshot lists 320×240, 640×480, 1280×720, 1920×1080). Exact YUY2 list: **UNVERIFIED**, enumerate on the device. | [B0268-DS], [B0268-QSG] |
| "4K video" | Marketing claim only. No 3840×2160 entry in the frame-rate table. **UNVERIFIED** | [ARD-prod] vs [B0268-DS] |
| Lens | M12 × P0.5 mount, EFL 2.72 mm, construction "5G+IR", integral IR-cut filter ("only visible light") | [B0268-DS] |
| FOV | **105° (H)** only. V and D FOV not published: **UNVERIFIED**. The datasheet's lens drawing is labelled "FOV:130° (IMX386)", apparently a generic drawing for another sensor, so do not use its dimensions without measuring. | [B0268-DS] |
| FOV model estimate | Sensor 5.21 × 3.92 mm. Rectilinear model: H 87.6°, V 71.5°, D 100.3°. Equidistant model: H ≈ 110°, V ≈ 82°, D ≈ 137°. The quoted 105° H is close to the equidistant model, so expect strong barrel distortion. (assessment) | derived from [B0268-DS] |
| Focus | Manual (M12 lens thread). Selection guide: "Manual Focus". Focusing range "1 m to infinity" per retail listing (secondary). | [ARD-sel], [ARD-prod] |
| Auto features | Saturation, Contrast, Acutance, White balance, Exposure | [B0268-DS] |
| UVC controls shown (Windows AMCap) | **Video Proc Amp:** Brightness, Contrast, Hue, Saturation, Sharpness, Gamma, White Balance (auto box ticked, 4600 shown), Backlight Comp, Gain, PowerLine Frequency (50 Hz shown). **Camera Control:** Exposure (auto ticked, value −6) and Low-Light Compensation are active. Zoom, Focus, Iris, Pan and Tilt are greyed out. Roll shows 0 but its status is unclear. Ranges and Linux V4L2 control names: **UNVERIFIED** (run `v4l2-ctl --list-ctrls-menus`). | [B0268-DS] §7 screenshots |
| Windows exposure units | DirectShow/KS exposure is "log base 2 seconds": −6 = 1/64 s, −7 = 1/128 s. Each driver defines its own range. On Windows, exposure therefore moves in doubling steps. | [MS-exp] |
| Power | DC 5 V, working current **max 200 mA** (≈1.0 W, derived) | [B0268-DS] |
| Operating temperature | −20 °C to +75 °C | [B0268-DS] |
| Reliability tests | 80 °C for 96 h, −20 °C for 96 h, 60 °C at 80–85 %RH, 60 cm drop ×10, 50 Hz / 1.5 mm vibration: "No abnormal" / "Electric normal" | [B0268-DS] |
| Board size / mounting | 38 × 38 mm. Hole pitch compatible with 34 × 34 mm and 28 × 28 mm. | [B0268-DS] |
| Cable / connector | Board-side connector ZHR-4 (detachable). Cable 1 m default, 2/3/5 m options. | [B0268-DS] |
| Microphone | B0268: **No** (selection guide). The autofocus IMX298 sibling B0290 has one. | [ARD-sel] |
| Related SKUs | B026801 (metal case, manual focus). B0290 / B029001 (IMX298 **autofocus**, mic). | [ARD-sel] |

---

## 2. Raspberry Pi 5 and AI accelerators

### 2.1 Power and USB

| Item | Verified value | Source |
|---|---|---|
| Recommended supply | 5 V / 5 A (25 W) via USB-C PD. Official 27 W PSU profiles: 5 V 3 A, 5 V 5 A, 9 V 3 A, 12 V 2.25 A, 15 V 1.8 A. | [RPI-PD-WP], [RPI-spec-doc] |
| PD behaviour | Only 5 V PDOs are requested. PPS is not supported. 5 V 5 A is an **optional** PD PDO, so many generic PD chargers lack it. Without PD, the board assumes 5 V 3 A. | [RPI-PD-WP] |
| USB current limit | **1.6 A total** to USB peripherals with a 5 A supply. **600 mA** otherwise. Without 5 A, USB boot is also disabled and the desktop shows a "power is limited" warning. | [RPI-PD-WP], [RPI-PSU-doc] |
| Overrides | EEPROM `PSU_MAX_CURRENT=5000` (skips PD and assumes 5 A). `config.txt` `usb_max_current_enable=1` (allows 1600 mA). Use these only with a supply that can really deliver it. | [RPI-PD-WP] |
| Typical bare-board active current | 800 mA | [RPI-PSU-doc] |
| Measured power (third-party, wall meter) | Off 1.7 W. Idle headless 3.0 W. All-core stress 8.8 W. Extreme multitask peak 16.8 W. | [CNX-pi5] (secondary) |

### 2.2 Environmental and thermal

| Item | Verified value | Source |
|---|---|---|
| Operating temperature | 0 °C to 70 °C | [RPI5-PB] |
| MTBF / lifetime | 93,800 h (ground benign). In production until at least January 2036. | [RPI5-PB] |
| Throttling | Arm cores progressively throttled between 80 °C and 85 °C. 85 °C is the SoC limit. | [RPI-spec-doc] |
| Active Cooler fan curve | Off below 50 °C. 30 % at 50 °C, 50 % at 60 °C, 70 % at 67.5 °C, 100 % at 75 °C. 5 °C hysteresis. | [RPI-spec-doc] |
| Cooling needed? | Raspberry Pi: cooling is "entirely optional" for normal use, but heavy continuous load can throttle. Blog measurement: idle about 65 °C uncooled vs about 45 °C with the Active Cooler. Required/recommended with AI HATs. | [RPI-blog-heat], [RPI-ai-doc] |
| On-board RTC | Present. Needs an external rechargeable Li-Mn coin cell (2-pin JST-SH). Charging is off by default (`dtparam=rtc_bbat_vchg=3000000`). Wake-alarm standby draws about 3 mA. **Accuracy not documented: UNVERIFIED.** | [RPI5-PB], [RPI-rtc-doc] |

### 2.3 40-pin header: GPIO, PWM and I2C

| Item | Verified value | Source |
|---|---|---|
| Logic level | 3.3 V outputs, inputs 3.3 V-tolerant only. "Do not use 5V for 3.3V components." GPIO2/3 have fixed pull-ups. | [RPI-gpio-doc] |
| Drive strength | Programmable 2–16 mA. All pads designed for 16 mA. The 3.3 V supply was designed for about 3 mA per GPIO average. | [RPI-pad-doc] |
| Hardware PWM | GPIO12, GPIO13, GPIO18, GPIO19 | [RPI-gpio-doc] |
| RP1 PWM detail | RP1 has two PWM blocks, and only PWM0 reaches bank 0. It has 4 channels and 32-bit counters. Mapping: PWM0[0..3] on GPIO12/13/14/15 (function a0). PWM0[2]/[3] are also on GPIO18/19 (a3). So GPIO12+13+18+19 give 4 independent channels. GPIO14/15 duplicate channels 2/3 and are the default UART pins. | [RP1-DS] Table 4, §3.4 |
| PIO PWM | `dtoverlay=pwm-pio`: up to 4 PIO-assisted PWM outputs on any GPIO 0–27, Pi 5 only | [OVL-README] |
| I2C on bank 0 | RP1 has 7 DesignWare I2C controllers, 4 of them on bank 0. I2C0: GPIO0/1 or 8/9. I2C1: GPIO2/3 or 10/11. I2C2: GPIO4/5 or 12/13. I2C3: GPIO6/7, 14/15 or 22/23. Modes: standard, fast (≤400 kbit/s), fast-mode plus (≤1000 kbit/s). Overlays: `i2c0-pi5` … `i2c3-pi5`. | [RP1-DS] §3.5, [OVL-README] |
| PPS input | `dtoverlay=pps-gpio`, default `gpiopin=18` | [OVL-README] |

### 2.4 AI accelerators (Hailo)

| Item | Verified value | Source |
|---|---|---|
| AI HAT+ | 13 TOPS (Hailo-8L, $70) or 26 TOPS (Hailo-8, $110). PCIe Gen 3. **Operating 0–50 °C ambient.** In production until at least January 2030. 16 mm stacking header supplied so it fits over the Active Cooler. | [AIHAT-PB] |
| AI Kit | M.2 HAT+ with Hailo-8L. Functionally equivalent to the AI HAT+ 13 TOPS, but **"no longer in production"**. | [RPI-ai-doc] |
| AI HAT+ 2 | Hailo-10H, 40 TOPS (INT4), 8 GB on-board RAM, $130, announced 15 January 2026. Vision performance "broadly equivalent" to the 26 TOPS AI HAT+. | [RPI-aihat2] |
| Hailo-8L chip | 13 TOPS, typical 1.5 W, −40 to 85 °C (chip grade). Frameworks: TensorFlow, TFLite, Keras, PyTorch, ONNX. | [HAILO-8L] |
| Runtime integration | Raspberry Pi OS auto-detects the NPU. `rpicam-apps` and Picamera2 can use it. An Active Cooler is recommended. | [RPI-ai-doc] |
| Model format / toolchain | Models must be compiled to **HEF** with the Hailo Dataflow Compiler (DFC). Inputs: ONNX or TFLite. | [RPI-DFC] |
| Where the DFC runs | Ubuntu 22.04/24.04 x86-64 (WSL2 also supported). "Hailo compilation requires Linux x86_64." Only the HEF is deployed to the Pi. | [HMZ-GS], [ULTRA-hailo] |
| Version split | Hailo-8/8L: Model Zoo **v2.x** + DFC **v3.x**. The master branch (DFC v5.x) is for Hailo-10/15. | [HMZ], [ULTRA-hailo] |
| Compile constraints | Optimisation needs calibration data (Ultralytics suggests ≥1024 representative images for production). Each HEF has a fixed input size. The HEF must target the exact device. | [HMZ-GS], [ULTRA-hailo] |
| CPU-only ONNX on Pi 5 | `onnxruntime` 1.30.0 publishes manylinux aarch64 wheels (cp311–cp314) on PyPI | [ORT-pypi] |

---

## 3. Microcontrollers

### 3.1 ESP32-WROOM-32E vs ESP32-S3-WROOM-1

| Item | ESP32 (WROOM-32E) | ESP32-S3 (WROOM-1) | Source |
|---|---|---|---|
| CPU | Xtensa dual-core, up to 240 MHz | Xtensa LX7 dual-core, up to 240 MHz | [ESP32-DS], [ESP32S3-DS] |
| On-chip SRAM / ROM | 520 KB SRAM, 448 KB ROM, 16 KB RTC SRAM | 512 KB SRAM, 384 KB ROM, 16 KB RTC SRAM | [ESP32-DS], [ESP32S3-DS] |
| PSRAM | None, except N4R2/N8R2/N16R2 = 2 MB in-package (uses IO16) | Up to 16 MB (2 MB quad or 8/16 MB octal, depending on variant) | [WROOM32E-DS], [S3WROOM1-DS] |
| Flash | 4/8/16 MB | Up to 16 MB | [WROOM32E-DS], [S3WROOM1-DS] |
| Native USB | None listed. DevKits use a USB-UART bridge. (assessment) | Full-speed USB OTG **and** USB Serial/JTAG controller "hardwired for CDC-ACM" (plug-and-play serial) | [ESP32-DS], [ESP32S3-DS] |
| LED PWM (LEDC) | 16 channels, up to 20-bit duty resolution | Up to 8 channels, duty resolution up to 14 bits | [ESP32-DS], [ESP32S3-DS] |
| Motor PWM | MCPWM present | MCPWM present | [ESP32-DS], [ESP32S3-DS] |
| I2C | 2 controllers. 100/400 kbit/s, "up to 5 MHz" pull-up-limited. Any GPIO via matrix. | 2 controllers. 100/400 kbit/s, up to 800 kbit/s. | [ESP32-DS], [ESP32S3-DS] |
| Radio | 802.11 b/g/n 2.4 GHz + Bluetooth v4.2 BR/EDR + BLE | 802.11 b/g/n 2.4 GHz + Bluetooth 5 (LE) | [ESP32-DS], [ESP32S3-DS] |
| Wi-Fi TX current | 802.11b 1 Mbps @19.5 dBm: **239 mA avg / 379 mA peak** (100 % duty) | 802.11b 1 Mbps @20.5 dBm: **355 mA peak** | [WROOM32E-DS], [S3WROOM1-DS] |
| Supply requirement | 3.0–3.6 V, external supply must deliver ≥0.5 A | ≥0.5 A | [WROOM32E-DS], [S3WROOM1-DS] |
| I/O levels | 3.3 V. VIH max = VDD + 0.3 V, so **not 5 V-tolerant** (assessment from DC table) | 3.3 V | [ESP32-DS] |
| Ambient temperature | −40–85 °C. H4/H8 variants (4/8 MB flash, no PSRAM): −40–105 °C. R2 (PSRAM) variants: −40–85 °C. | −40–85 °C typical. **R8 and R16V: −40–65 °C** (85 °C with PSRAM ECC, losing 1/16 of PSRAM). H4: −40–105 °C. | [WROOM32E-DS], [S3WROOM1-DS] |
| Lifecycle | Original **ESP32-WROOM-32 is NRND**. Use WROOM-32E. | Active (module datasheet v1.8) | [WROOM32-NRND], [S3WROOM1-DS] |

### 3.2 Arduino Uno R3 vs Uno R4 Minima / WiFi

| Item | Uno R3 (ATmega328P) | Uno R4 Minima | Uno R4 WiFi | Source |
|---|---|---|---|---|
| MCU / clock | ATmega328P, 16 MHz | Renesas RA4M1 (Cortex-M4F), 48 MHz | Same RA4M1 + ESP32-S3-MINI-1-N8 coprocessor (3.3 V, via level translator) | [UNO-R3-DS], [UNO-R4M-DS], [UNO-R4W-DS] |
| Flash / SRAM / EEPROM | 32 KB / **2 KB** / 1 KB | 256 KB / 32 KB / 8 KB | 256 KB / 32 KB / 8 KB | [UNO-R3-DS], [M328P-DS], [UNO-R4M-DS], [UNO-R4W-DS] |
| Logic level | 5 V | 5 V | 5 V (RA4M1). Qwiic port is 3.3 V. | same |
| PWM pins | 6 | 6 (D3, D5, D6, D9, D10, D11) | 6 | [UNO-R3-page], [UNO-R4M-DS] |
| I2C | 1 (A4/A5) | 1 (A4/A5) | 2: `Wire` on A4/A5, `Wire1` on Qwiic | same |
| USB | USB-B via ATmega16U2 bridge | USB-C, native (HID capable) | USB-C | same |
| Per-pin current | Abs. max 40 mA per pin, 200 mA total VCC/GND. Test conditions 20 mA at 5 V. | **8 mA** | **8 mA** | [M328P-DS], [UNO-R4M-DS], [UNO-R4W-DS] |
| VIN | Barrel jack (VIN range not restated in fetched text) | 6–24 V | 6–24 V | [UNO-R4M-DS], [UNO-R4W-DS] |
| Temperature | −40–85 °C ("conservative thermal limits") | −40–85 °C | −40–85 °C | same |
| Extras | — | 12-bit DAC, CAN (external transceiver needed), RTC | + 12×8 LED matrix, Wi-Fi/BLE | same |

**Arduino Servo library limits:** `SERVOS_PER_TIMER 12`. Defaults `MIN_PULSE_WIDTH 544`, `MAX_PULSE_WIDTH 2400`, `REFRESH_INTERVAL 20000` µs ([SERVO-h]). On AVR, Timer1 runs with prescaler 8 (`usToTicks = clockCyclesPerMicrosecond()*us/8`), which is 0.5 µs per tick at 16 MHz (derived) ([SERVO-avr]). On non-Mega boards the library disables `analogWrite()` on pins 9 and 10 ([SERVO-ref], via search index because the page body did not render).

---

## 4. PCA9685 16-channel PWM driver

| Item | Verified value | Source |
|---|---|---|
| Channels / resolution | 16 outputs, 12-bit (4096 steps) each. All outputs share one frequency. | [PCA9685-DS] |
| Frequency | Programmable from "typical 24 Hz to 1526 Hz". `prescale = round(osc/(4096·rate)) − 1`. Minimum prescale 3 (1526 Hz). | [PCA9685-DS], [ADA-PWM-h] |
| Oscillator | Internal "25 MHz typical" (no tolerance given in the datasheet). EXTCLK input up to 50 MHz. The Adafruit library exposes `setOscillatorFrequency()` for per-board trimming. | [PCA9685-DS], [ADA-PWM-h] |
| Output drive | Open-drain 25 mA sink at 5 V, or totem-pole 25 mA sink / 10 mA source at 5 V. I/O 5.5 V-tolerant. | [PCA9685-DS] |
| Supply | VDD 2.3–5.5 V. Tamb −40 to +85 °C. | [PCA9685-DS] |
| I2C | Fast-mode Plus up to 1 MHz. 6 address pins give 64 addresses, of which 62 are usable (LED All Call 1110 000 = 0x70 and Software Reset are reserved). | [PCA9685-DS] |
| Power-on state | POR default for LEDn outputs is LOW (no pulses until configured). OE pin available. | [PCA9685-DS] |
| Servo-pulse quantisation | 50 Hz: 20 ms / 4096 = **4.88 µs per count**. ~200 Hz: ≈1.22 µs. Adafruit quotes "~4 µs at 60 Hz". (derived) | [PCA9685-DS], [ADA-815] |
| Adafruit #815 breakout | Address 0x40–0x7F by jumpers (62 boards / 992 outputs). Terminal block for servo V+ with reverse-polarity protection. 220 Ω series resistor on every output. 5 V-compliant logic, usable from 3.3 V MCUs. 62.5 × 25.4 × 3 mm. 5.5 g bare, 9 g with headers. | [ADA-815] |
| VCC vs V+ | VCC = logic power for the breakout only, "NOT the servo power". Servos "run on about 5 or 6v". Bulk capacitor "n × 100 µF" (470 µF+ for 5 servos). High-torque servos ">1 A each under load". | [ADA-PWM-hook] |
| V+ absolute maximum | Not stated on the Adafruit pages read: **UNVERIFIED** | — |

---

## 5. IMUs

| Item | MPU-6050 | ICM-20948 | BNO085 / BNO086 | BMI270 | LSM6DSOX |
|---|---|---|---|---|---|
| Status | **Obsolete** on TDK's status field (secondary; TDK pages blocked). Last-buy dates: **UNVERIFIED**. | **EOL** on TDK's status field (secondary) | Current CEVA datasheet v1.17 (2023). Lifecycle: **UNVERIFIED** | Listed by Bosch (page notes distributor stock-outs) | **UNVERIFIED** (st.com timed out) |
| Sensors | 6-axis | 9-axis: gyro/accel/DMP die + AK09916 magnetometer die. Gyro ±250…±2000 dps. Compass ±4900 µT. | Bosch accel (12-bit, ±8 g), gyro (16-bit, ±2000 dps), magnetometer + Cortex-M0+ running CEVA SH-2 fusion | 6-axis. ±2…±16 g. ±125…±2000 dps. Gyro noise 0.007 dps/√Hz. AUX I2C for an external magnetometer. | 6-axis. ±2…±16 g. ±125…±2000 dps. Machine-learning core. 9 KB FIFO. Sensor hub. |
| Supply / I/O | **UNVERIFIED** (not fetched) | VDD 1.71–3.6 V. **VDDIO 1.71–1.95 V.** | VDD (sensors) 2.4–3.6 V. VDDIO 1.7–3.6 V. | VDD 1.7–3.6 V. VDDIO 1.2–3.6 V. | 1.71–3.6 V |
| Host interface | I2C | 400 kHz I2C or 7 MHz SPI | I2C (**master must support clock stretching**), SPI, UART-SHTP, UART-RVC (115200 baud, 8N1) | 2× SPI, 2× I2C, AUX, OIS | SPI / I2C / MIPI I3C |
| Temperature | — | −40 to +85 °C | −40 to +85 °C | −40 to +85 °C | −40 to +85 °C |
| Sources | [OBS-brief] | [ICM20948-DS], [OBS-brief] | [BNO08x-DS] | [BMI270] | [LSM6DSOX-DS] (secondary: values from search index of ST datasheet) |

**BNO08x fusion accuracy** ([BNO08x-DS] Fig. 6-14; generated by simulation over 210 characterised devices):

| Output | Dynamic | Static |
|---|---|---|
| Rotation Vector (9-axis, mag-referenced) | 3.5° rotation error | 2.0° |
| Game Rotation Vector (no magnetometer) | 2.5° non-heading error | 1.5° |
| Geomagnetic Rotation Vector | heading drift 0.5°/min | — |
| Gravity | 4.5° | 3.0° |

**BNO08x calibration and magnetometer notes** ([BNO08x-DS]):
- Hard-iron (magnets, speakers) and soft-iron (ferrous material) distortions are compensated by dynamic calibration. Without calibration, "the heading … will be highly suspect".
- Calibration status is reported in the 2-bit Status field: 0 unreliable, 1 low, 2 medium, 3 high.
- Forced-calibration procedure: accelerometer, 4–6 orientations held about 1 s each. Gyroscope, 2–3 s stationary. Magnetometer, rotate about 180° and back on each axis, about 2 s per axis.
- For "an unstable magnetic field" the datasheet recommends the **Game Rotation Vector**.
- Dynamic calibration data is saved to RAM every 5 s. The BNO086 adds "Interactive Calibration" for lower heading drift.

**Adafruit BNO085 breakout** ([ADA-BNO085]): on-board regulator (3–5 V in) and level shifting. I2C address 0x4A (0x4B with DI high). Recommends `dtparam=i2c_arm_baudrate=400000` on Raspberry Pi. **Uno/Leonardo "will not work due to minimal RAM"**. A separate UART-RVC library exists for small MCUs.

---

## 6. GNSS, RTC and environment sensor

| Item | u-blox NEO-M8N | u-blox NEO-M9N | Source |
|---|---|---|---|
| Time pulse (PPS) | Configurable 0.25 Hz–10 MHz, default 1 PPS. Accuracy **RMS 30 ns, 99 % 60 ns**. | 0.25 Hz–10 MHz. **RMS 30 ns, 99 % 60 ns**. TIMEPULSE drive 4 mA. | [M8-DS], [M9N-DS] |
| Protocols | NMEA 0183 v4.0 (2.1/2.3/4.1 configurable), UBX, RTCM input (msgs 1, 2, 3, 9) | UBX, NMEA 4.10 (default)/4.0/2.3/2.1, RTCM 3.3 input | [M8-DS], [M9N-DS] |
| Interfaces | UART, USB, SPI, DDC (I2C-compliant) | UART, SPI, I2C, USB | [M8-DS], [M9N-DS] |
| Supply | 2.7–3.6 V (product overview table) | 2.7–3.6 V | [M8-DS], [M9N-DS] |
| Current | — | Peak 100 mA (acquisition). Tracking (continuous, 4-GNSS) 36 mA. Power-save tracking 21 mA (at 3.0 V). | [M9N-DS] |
| Operating temperature | −40 to +85 °C | −40 to +85 °C | [M8-DS], [M9N-DS] |
| Lifecycle | Datasheet R14 lists PCN references for M8 firmware (status not restated) | "Mass production" | [M8-DS], [M9N-DS] |

**DS3231 RTC** ([DS3231-DS], Maxim 19-5170 Rev 7):
- Frequency stability: **±2 ppm at 0–40 °C**. **±3.5 ppm** above 40 °C (to +70 °C for the commercial DS3231S, to +85 °C for the industrial DS3231SN) and below 0 °C (SN).
- 2 ppm ≈ 0.17 s/day and 3.5 ppm ≈ 0.30 s/day (derived).
- Crystal aging ±1.0 ppm in the first year and ±5.0 ppm over 0–10 years (not production tested).
- Temperature sensor ±3 °C. VCC 2.3–5.5 V, VBAT 2.3–5.5 V. 400 kHz I2C.

**BME280** ([BME280-DS], rev 1.23):

| Parameter | Value |
|---|---|
| Supply | VDD 1.71–3.6 V. VDDIO 1.2–3.6 V. I2C and SPI. |
| Operating range | −40…+85 °C, 0…100 %RH, 300…1100 hPa. The operating range applies only to a **non-condensing** environment. Reconditioning is needed after exceeding it. |
| Full-accuracy range | 0…65 °C |
| Humidity | ±3 %RH absolute (20–80 %RH, 25 °C). Hysteresis ±1 %RH. τ63 = 1 s. Long-term drift 0.5 %RH/year. |
| Pressure | ±1.0 hPa absolute (0–65 °C). ±1.7 hPa (−20–0 °C). Relative ±0.12 hPa (700–900 hPa, 25–40 °C). Long-term ±1.0 hPa/year. |
| Temperature | ±0.5 °C (0–65 °C). ±1.25 °C (−20–0 °C). ±1.5 °C (−40…−20 °C). |
| Current | 3.6 µA at 1 Hz (H+P+T). 0.1 µA in sleep. |

---

## 7. Actuators: hobby servos vs steppers with TMC2209

### 7.1 Servos

| Item | TowerPro MG996R | DSServo DS3218 | Source |
|---|---|---|---|
| Source quality | Official manufacturer page | Reseller listing (no manufacturer page found) **(secondary)** | [TP-MG996R], [RCD-DS3218] |
| Operating voltage | 4.8–6.6 V | 4.8–6.8 V | same |
| Stall torque | 9.4 kg·cm @4.8 V. 11 kg·cm @6.0 V. | 19 kg·cm @5 V. 21.5 kg·cm @6.8 V. | same |
| Speed | 0.19 s/60° @4.8 V. 0.15 s/60° @6.0 V. | 0.16 s/60° @5 V. 0.14 s/60° @6.8 V. | same |
| Dead band | 1 µs | 3 µs | same |
| Travel | **0–159°** (manufacturer reply on product page) | 180° or 270° versions, pulse 500–2500 µs | same |
| Current | Idle 10 mA. No-load 170 mA. **Stall 1400 mA.** | Idle 4–5 mA. **Stall: UNVERIFIED** (resellers quote about 2 A). | same |
| Temperature | **0–55 °C** | −25–70 °C | same |
| Ingress | — | IP66 (reseller claim) | [RCD-DS3218] |
| Mass / size | 55 g. 40.7 × 19.7 × 42.9 mm. | 60 g. 40 × 20 × 40.5 mm. | same |
| Notes | TowerPro warns of widespread counterfeits on eBay/Amazon/Alibaba | — | [TP-MG996R] |

**Realistic pointing resolution with servos (assessment):**
- DS3218-180: 2000 µs / 180° ≈ 11.1 µs/°, so a 3 µs deadband is about 0.27°.
- DS3218-270: ≈7.4 µs/°, so 3 µs is about 0.4°.
- The PCA9685 at 50 Hz (4.88 µs per count) adds 0.44–0.66° quantisation.
- Gear backlash, potentiometer linearity and temperature drift are not published (**UNVERIFIED**).
- About 0.5–1° repeatability is a plausible target. 0.1° is not credible with hobby servos.

### 7.2 Steppers and TMC2209

| Item | Verified value | Source |
|---|---|---|
| Typical NEMA 17 (17HS4401S) | 1.8°/step. 1.7 A/phase. 43 N·cm holding. 1.5 Ω and 2.8 mH per phase. 40 mm length. 280 g. | [HT-17HS4401S] (secondary, distributor) |
| Step accuracy | Oriental Motor standard steppers: ±3 arcmin (0.05°), **non-cumulative**. Microstepping reduces torque "usually by about 30%". Resonance "usually … around 200Hz". | [OM-basics] |
| Generic NEMA 17 accuracy | ±5 % of a full step is commonly quoted: **UNVERIFIED** (vendor page blocked) | — |
| TMC2209 ratings | 2 A RMS / 2.8 A peak. 4.75–29 V DC. Low RDSon (170 mΩ typical, LS and HS). | [TMC2209-DS] |
| Microstepping | STEP/DIR with MS1/MS2 pins: 00 = 1/8, 01 = 1/32, 10 = 1/64, 11 = 1/16. 256 microsteps via MicroPlyer interpolation. | [TMC2209-DS] |
| Choppers | StealthChop2 (silent) and SpreadCycle (dynamic). CoolStep (up to 75 % energy saving). | [TMC2209-DS] |
| UART | Single-wire UART + OTP. Slave address set by MS1/MS2, so up to 4 drivers share a bus. Baud ≥9000, maximum fCLK/16. | [TMC2209-DS] |
| StallGuard4 | Designed for StealthChop. SG_RESULT updates once per full step. A stall means losing 4 full steps. DIAG pulses only in StealthChop when TCOOLTHRS ≥ TSTEP > TPWMTHRS. **Unreliable at very low speed** ("less than one revolution per second" for many motors) and at very high speed. A mechanical home switch gives "more precise homing". | [TMC2209-DS] §11 |
| Datasheet revision read | Rev 1.03 (2019-06-26), mirror. ADI's rev 1.09 URL was unreachable. | [TMC2209-DS] |

**Servo vs stepper for a 100–200 g camera at 0.1–1° (assessment):**

| | Hobby servo + PCA9685 | NEMA 14/17 + TMC2209 |
|---|---|---|
| Absolute position | Yes (internal potentiometer). No homing needed. | No. Needs homing (switch or Hall sensor). StallGuard alone is too coarse (≥4 full steps) and too slow-speed-sensitive for 0.1°. |
| Resolution / repeatability | About 0.3–1° (deadband plus PCA9685 quantisation) | 1.8°/16 = 0.11° per microstep direct. With a 3:1–5:1 belt reduction, about 0.02–0.04°. Accuracy is bounded by step accuracy (~0.05° per OM) divided by the reduction. |
| Range | MG996R 159°. DS3218 180° or 270°. | Unlimited (needs a slip ring or cable wrap limit for pan) |
| Holding | Active hold with buzz/hunting. Draws current while loaded. | Holding torque at rest. Can reduce standstill current. |
| Power / supply | 5–6 V rail, about 1.4–2 A stall per servo | 12–24 V rail, up to 1.7 A/phase per motor at NEMA 17 sizes |
| Complexity | Lowest. Works on Uno/ESP32/Pi. | Needs STEP/DIR timing and UART configuration. Best on ESP32 or Pi + driver. |
| Magnetic impact on IMU | DC motor magnets (smaller) | Permanent-magnet rotors close to the head: strong hard-iron source |
| Verdict | Fine for the 1° tier and the Uno/Pi-native tiers | Required for the 0.1° goal |

---

## 8. Outdoor enclosure (Bengaluru)

### 8.1 Print materials

| Material (grade) | Tg (DSC) | HDT 0.45 / 1.8 MPa | Vicat | UV statement | Source |
|---|---|---|---|---|---|
| PLA (PolyLite PLA) | 61 °C | 60 / 58 °C | 63 °C | None in TDS | [PM-PLA] |
| PETG (PolyLite PETG) | 81 °C | 78 / 75 °C | 84 °C | None in TDS | [PM-PETG] |
| ASA (Polymaker ASA) | 98 °C | 103 / 100 °C | 105 °C | "improved weather resistance", "UV resistance" (no test data given) | [PM-ASA] |

### 8.2 Window materials

| Material | Verified facts | Source |
|---|---|---|
| Acrylic (PMMA, PLEXIGLAS) | Visible transmission up to 92 %. Clear sheet guaranteed to keep up to 90 % transmission and not yellow for 30 years. Standard sheet is UPF 50+ (≥98 % of UV blocked). Special UV-transmitting grades exist. | [PLEX-LT], [PLEX-UV] |
| Polycarbonate (Makrolon) | High transmission in the visible and NIR up to about 1100 nm (87–90 % depending on grade). Absorbs UV. The main weathering failure is **yellowing and haze**, so use a UV-protected grade or coating. | [MAK-opt] |
| Glass | No primary source read. UV cut-off and transmission: **UNVERIFIED**. | — |
| Relevance | The B0268 has an integral IR-cut filter, so window NIR transmission does not matter for this camera. Window UV absorption does not affect visible imaging either. Scratch and yellowing resistance dominate the choice. (assessment) | [B0268-DS] |

### 8.3 Dew control, sealing and glands

- **Dew heaters used in commercial all-sky cameras:** 12 V 0.18 A (2.1 W) ring, 52 mm ID / 72 mm OD, mounted around the camera inside the dome ([ASO-dew]). Another product: 12 V 0.23 A (2.8 W), same ring size ([DC-dew]).
- **IP code** (IEC 60529, via secondary summary because the IEC page returned 403):
  - IP5x: dust-protected. IP6x: dust-tight.
  - IPx4: splashing water.
  - IPx5: 6.3 mm nozzle, 12.5 L/min, ≥3 min.
  - IPx6: 12.5 mm nozzle, 100 L/min, ≥3 min.
  - IPx7: 1 m immersion for 30 min.
  - Source: [IPCODE].
- **Cable gland example:** LAPP SKINTOP BS PG7, 2.5–6.5 mm clamping range, IP68, UV-resistant black polyamide ([LAPP-BS], secondary). The B0268 cable unplugs at the board (ZHR-4), so pass the bare cable through the gland and plug it in inside. The USB-A plug never has to pass through (assessment from [B0268-DS]).

### 8.4 Bengaluru climate

IMD climatological table, period 1991–2020, station id 43295 ([IMD-43295]). The page does not print the station name. Its values match the IMD-cited Bangalore table on Wikipedia ([WIKI-blr]).

| Month | Mean daily max (°C) | Mean daily min (°C) | Rainfall (mm) | Rainy days | Thunder days |
|---|---|---|---|---|---|
| Jan | 28.4 | 16.1 | 1.6 | 0.2 | 0.0 |
| Mar | 33.4 | 20.2 | 14.7 | 1.1 | 1.7 |
| **Apr** | **34.1** | 22.1 | 61.7 | 4.0 | 7.2 |
| **May** | **33.1** | 21.8 | 128.7 | 7.5 | 10.9 |
| Jun | 29.7 | 20.6 | 110.3 | 6.8 | 4.4 |
| Jul | 28.3 | 20.1 | 116.4 | 8.0 | 2.9 |
| Aug | 28.1 | 20.0 | 162.7 | 10.2 | 4.7 |
| **Sep** | 28.6 | 20.0 | **208.3** | 9.5 | 6.3 |
| Oct | 28.5 | 19.8 | 186.4 | 9.6 | 7.0 |
| Nov | 27.4 | 18.3 | 64.5 | 4.2 | 1.0 |
| Dec | 26.9 | 16.4 | 15.4 | 1.3 | 0.0 |
| Annual | 29.8 | 19.4 | 1077.6 | 62.6 | 46.4 |

- **Record high:** 39.2 °C on 24 April 2016. Record low: 7.8 °C in January 1884 ([WIKI-blr], secondary citing IMD).
- **Relative humidity at 17:30 IST:** 29 % (March) to 67 % (August) ([WIKI-blr], secondary). IMD RH normals were not retrieved: **UNVERIFIED** at primary level.
- **Seasons:** southwest monsoon June–September, post-monsoon (northeast) October–November ([WIKI-blr]).
- **Fog days** (IMD table): January 2.0, December 1.3 ([IMD-43295]).
- **Solar heating** of a closed enclosure above ambient was not found in any source: **UNVERIFIED**. Measure it in P094.

---

## 9. Power budget inputs

| Load | Rail | Typical | Peak / stall | Source |
|---|---|---|---|---|
| Arducam B0268 | 5 V USB | — | ≤200 mA (≤1.0 W) | [B0268-DS] |
| Raspberry Pi 5 (bare) | 5 V | 800 mA typical active (≈4 W). Idle about 3.0 W (wall). | 8.8 W stress. 16.8 W extreme multitask (wall). | [RPI-PSU-doc], [CNX-pi5] |
| Pi 5 USB downstream budget | 5 V | — | 1.6 A total (5 A PSU) or 0.6 A | [RPI-PD-WP] |
| Hailo-8L (AI HAT+ 13 TOPS) | via Pi | 1.5 W typical (chip). Board-level: **UNVERIFIED**. | — | [HAILO-8L] |
| ESP32-WROOM-32E | 3.3 V | — | 239 mA avg / 379 mA peak (Wi-Fi TX). Supply ≥0.5 A. | [WROOM32E-DS] |
| ESP32-S3-WROOM-1 | 3.3 V | — | 355 mA peak (Wi-Fi TX). Supply ≥0.5 A. | [S3WROOM1-DS] |
| Uno R3 board | 5 V | **UNVERIFIED** (datasheet shows "xx mA") | — | [UNO-R3-DS] |
| NEO-M9N | 3.0–3.3 V | 36 mA tracking | 100 mA peak | [M9N-DS] |
| BME280 / DS3231 / PCA9685 logic | 3.3 V | µA–mA range (BME280 3.6 µA at 1 Hz) | — | [BME280-DS] |
| MG996R (each) | 4.8–6.6 V | 170 mA no-load | **1.4 A stall** (≈8.4 W at 6 V, derived) | [TP-MG996R] |
| DS3218 (each) | 4.8–6.8 V | 4–5 mA idle | **UNVERIFIED** (about 2 A per resellers) | [RCD-DS3218] |
| NEMA 17 + TMC2209 (each) | 12–24 V typical (driver range 4.75–29 V) | Set by run/hold current | ≤1.7 A/phase (17HS4401S). Driver ≤2 A RMS. | [HT-17HS4401S], [TMC2209-DS] |
| Dome dew heater | 12 V | 2.1–2.8 W (continuous, or PWM duty) | — | [ASO-dew], [DC-dew] |

**Servo-rail sizing (assessment):**
- **Voltage:** a dedicated regulated 6.0 V rail, within both the MG996R limit (6.6 V) and the DS3218 limit (6.8 V).
- **Current:** at least 5 A, so that both servos can stall at once (2 × 1.4–2 A) with margin.
- **Capacitance:** bulk capacitance of at least n × 100 µF (Adafruit), with 470–1000 µF at the PCA9685 V+ terminal.
- **Ground:** common ground with the logic. The rail must never pass through the Pi or MCU 5 V pins.

**Whole-node estimate (Pi tier, assessment):**

| Load | Power |
|---|---|
| Pi 5 | 4–9 W |
| B0268 | 1 W |
| Hailo-8L | 1.5 W |
| Dew heater | 2–3 W |
| Sensors | < 0.5 W |
| Servos (average) | a few W |
| Servos (stall transient) | up to about 17 W |

A 12 V input with separate buck converters fits this load: 5 V/5 A for the Pi (or the official 27 W PD supply) and 6 V/≥5 A for the servos. A 12 V supply of at least 4–5 A gives headroom.

---

## 10. Implications for the CloudScope design

1. **Uno tier stays "lite".**
   - 2 KB SRAM rules out the BNO08x SH-2 library and any on-board fusion plus the full CSDP protocol.
   - The MPU-6050 named in PLAN.md is reported obsolete. For a new Uno build, either omit the IMU, or use a BNO085 in **UART-RVC** mode (heading/pitch/roll at 115200 baud), or step up to an **Uno R4 Minima** (32 KB SRAM).
   - On the R4, keep loads at or below 8 mA per pin.
   - The Servo library blocks PWM on D9/D10.
2. **ESP32 tier.**
   - Specify **ESP32-WROOM-32E** or **ESP32-S3-WROOM-1 (N8/N16 or N16R2)**. The S3 is preferred because its native USB CDC simplifies the host link.
   - Avoid S3 R8/R16V parts in the outdoor head unless PSRAM ECC is enabled (65 °C limit).
   - Give the 3.3 V rail at least 0.5 A to absorb about 380 mA Wi-Fi peaks.
   - The ESP32 LEDC (16 channels, up to 20-bit) can drive servos directly with sub-µs resolution. The PCA9685 is still useful to offload timing and to protect GPIOs.
3. **Pi 5 native tier.**
   - Use the official 27 W PSU, or a known 5 V/5 A source with `PSU_MAX_CURRENT=5000`, whenever a USB disk is attached.
   - Four hardware PWM channels are available on GPIO12/13/18/19, and `pwm-pio` adds up to 4 more on any bank-0 GPIO.
   - Move `pps-gpio` off GPIO18 if GPIO18 drives a servo.
   - GPIO is 3.3 V only. Never wire 5 V servo signal or sensor outputs back into it.
4. **Thermal envelope for an outdoor head in Bengaluru.**
   - Component limits: AI HAT+ 50 °C, MG996R 55 °C, S3-R8 65 °C, Pi 5 70 °C, B0268 75 °C, DS3218 70 °C.
   - Climate: April/May mean daily maximum 33–34 °C, record 39.2 °C, plus solar gain.
   - Keep the Pi 5 and AI HAT+ in a shaded or indoor box and run USB (B0268 cable options up to 5 m) to the head.
   - Use white ASA, a sun shield and a measured temperature log (P094). Do not use MG996R where internal temperatures can pass 55 °C.
5. **Separate servo rail is mandatory.** One MG996R stalls at 1.4 A, which already exceeds the entire 600 mA Pi USB budget, and Adafruit warns high-torque servos exceed 1 A each. Size the rail at 6 V, ≥5 A, with bulk capacitance and a common ground (§9).
6. **Accuracy tiers.**
   - The **1° tier** works with digital servos (DS3218-class, 3 µs deadband) plus calibration.
   - The **0.1° tier** needs steppers with belt or gear reduction, a home switch or Hall sensor (StallGuard4 is not precise enough and fails at low speed), and sky-based absolute calibration (Sun/star astrometry).
   - With the MG996R's 159° travel, a full sky survey is impossible. Use 270° servos with tilt flip-over, or a continuous stepper pan.
7. **PCA9685 settings.** At 50 Hz each count is 4.88 µs, worse than the servo deadband. Raising the frame rate improves resolution (about 1.2 µs at 200 Hz) but only for servos that accept it (the DS3218 listing mentions 333 Hz; verify on hardware). Calibrate the oscillator per board, because the datasheet gives only "25 MHz typical". Outputs default LOW at power-up, which is a safe start.
8. **IMU strategy.**
   - Do not rely on magnetometer heading near servo or stepper magnets. Use the **Game Rotation Vector** (2.5° dynamic / 1.5° static non-heading error) for level and tilt, and derive azimuth from Sun/star solving.
   - BNO085 on Pi or ESP32: the I2C master must clock-stretch. Use 400 kHz on the Pi, or use SPI/UART.
   - ICM-20948 is EOL and has 1.8 V I/O, so drop it.
   - BMI270 and LSM6DSOX are current 6-axis fallbacks with no magnetometer.
9. **Camera software assumptions.**
   - Full resolution is MJPG only, so radiometric work (STRATIA calibration) should use YUY2 at ≤1024×768, or accept MJPEG compression.
   - Plan intrinsic calibration for a near-fisheye lens.
   - On Windows, exposure moves in log2 steps (−6 = 1/64 s). Enumerate actual ranges per OS in P018/P020.
   - Focus is manual, so lock the M12 thread after focusing at infinity.
10. **Timekeeping.** GNSS PPS (30 ns RMS) with `pps-gpio` gives sub-µs time on the Pi. A DS3231 (≤0.17–0.30 s/day) is the holdover clock for ESP32/Uno tiers. The Pi 5 on-board RTC needs a battery and has no published accuracy.
11. **Enclosure materials.**
    - Print parts in ASA (Tg 98 °C). PETG (Tg 81 °C) is acceptable only for shaded internal parts. Do not use PLA (Tg 61 °C) outdoors.
    - Window: PMMA or glass. Use polycarbonate only in a UV-protected grade.
    - Fit a 2–3 W, 12 V dome or window heater under PWM control from a dew-point margin.
    - Target IP65-class sealing with IP68 glands.
    - Keep the BME280 out of condensation (non-condensing rating) and out of the heater's thermal plume.
12. **Plan text to correct in P009/P010.**
    - "Hailo-8L AI Kit" should become "AI HAT+ (13/26 TOPS)", or the AI HAT+ 2 for GenAI.
    - Add an x86-64 Linux DFC v3.x compile step to the model pipeline.
    - "raw MPU-6050" in the Uno tier: the part is obsolete, so list a replacement.

---

## 11. UNVERIFIED register

| Item | Why |
|---|---|
| B0268 vertical/diagonal FOV, exact YUY2 mode list, 4K mode, UVC control ranges, Linux control names | Not in datasheet. Measure on the device (`v4l2-ctl --list-formats-ext`, `--list-ctrls-menus`). |
| B0268 focus range "1 m to infinity" | Only from a search snippet of the blocked Arducam page |
| Pi 5 on-board RTC accuracy | Not documented |
| AI HAT+ board-level power | Only chip-level 1.5 W (Hailo-8L) found |
| MPU-6050 / ICM-20948 status dates | TDK pages blocked. Status comes from a secondary source with no dates. |
| MPU-6050 electrical specs | Not fetched (part obsolete) |
| LSM6DSOX lifecycle and exact VDDIO range | st.com timed out. Values from search index. |
| DS3218 stall current, manufacturer datasheet | No manufacturer page found. Reseller data only. |
| Servo gear backlash and linearity | Not published by either vendor |
| Generic NEMA 17 ±5 % step accuracy | Vendor page blocked |
| PCA9685 oscillator tolerance | Not given in NXP datasheet |
| Adafruit PCA9685 V+ maximum | Not stated on pages read |
| Glass window UV/IR transmission | No primary source read |
| Enclosure solar heat gain in Bengaluru | No source. Measure in P094. |
| Bengaluru RH normals at primary level | IMD table read lacks RH. Wikipedia cites IMD. |
| BNO085 + RP1 I2C clock-stretching behaviour | RP1 uses Synopsys DW_apb_i2c. Clock-stretch compatibility with BNO08x not confirmed. |
| Uno R3 board current | Datasheet field reads "xx mA" |

---

## 12. Sources

**Camera**

[B0268-DS]: https://www.uctronics.com/download/Amazon/B0268_16MP_Wide_Angle_UVC_Camera_Datasheet.pdf
[B0268-QSG]: https://www.uctronics.com/download/Amazon/B0268.pdf
[ARD-sel]: https://docs.arducam.com/UVC-Camera/USB2-UVC-Camera-Kit/Specs-and-Selection-Guide/
[ARD-prod]: https://www.arducam.com/product/arducam-16mp-wide-angle-usb-camera-for-laptop-1-2-8-cmos-imx298-mini-uvc-b0268/
[MS-exp]: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-exposure

- B0268-DS — Arducam datasheet "IMX298 USB2.0 Wide Angle Camera Module B0268", V1.0, 26.08.2020: https://www.uctronics.com/download/Amazon/B0268_16MP_Wide_Angle_UVC_Camera_Datasheet.pdf
- B0268-QSG — Arducam B0268 Quick Start Guide: https://www.uctronics.com/download/Amazon/B0268.pdf
- ARD-sel — Arducam USB2 UVC camera selection guide: https://docs.arducam.com/UVC-Camera/USB2-UVC-Camera-Kit/Specs-and-Selection-Guide/
- ARD-prod — Arducam B0268 product page (403/Cloudflare to automated fetch; title from search index): https://www.arducam.com/product/arducam-16mp-wide-angle-usb-camera-for-laptop-1-2-8-cmos-imx298-mini-uvc-b0268/
- MS-exp — Microsoft, KSPROPERTY_CAMERACONTROL_EXPOSURE: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-exposure

**Raspberry Pi 5 / Hailo**

[RPI-PD-WP]: https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-009856-WP-1-USB%20Power%20delivery%20on%20Raspberry%20Pi%205.pdf
[RPI5-PB]: https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf
[RPI-PSU-doc]: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/power-supplies.adoc
[RPI-spec-doc]: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
[RPI-blog-heat]: https://www.raspberrypi.com/news/heating-and-cooling-raspberry-pi-5/
[RPI-gpio-doc]: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/gpio-on-raspberry-pi.adoc
[RPI-pad-doc]: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/gpio-pad-controls.adoc
[RPI-rtc-doc]: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/rtc.adoc
[RP1-DS]: https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf
[OVL-README]: https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/overlays/README
[CNX-pi5]: https://www.cnx-software.com/2023/11/05/raspberry-pi-5-review-raspberry-pi-os-bookworm-benchmarks-power-consumption/
[AIHAT-PB]: https://datasheets.raspberrypi.com/ai-hat-plus/raspberry-pi-ai-hat-plus-product-brief.pdf
[RPI-ai-doc]: https://www.raspberrypi.com/documentation/accessories/ai-hat-plus.html
[RPI-aihat2]: https://www.raspberrypi.com/news/introducing-the-raspberry-pi-ai-hat-plus-2-generative-ai-on-raspberry-pi-5/
[HAILO-8L]: https://hailo.ai/products/ai-accelerators/hailo-8l-ai-accelerator-for-ai-light-applications/
[RPI-DFC]: https://www.raspberrypi.com/news/raspberry-pi-ai-kit-update-dataflow-compiler-now-available/
[HMZ]: https://github.com/hailo-ai/hailo_model_zoo
[HMZ-GS]: https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/GETTING_STARTED.rst
[ULTRA-hailo]: https://docs.ultralytics.com/integrations/hailo
[ORT-pypi]: https://pypi.org/project/onnxruntime/

- RPI-PD-WP — Raspberry Pi white paper "USB Power Delivery on Raspberry Pi 5" (Release 1, March 2026): https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-009856-WP-1-USB%20Power%20delivery%20on%20Raspberry%20Pi%205.pdf
- RPI5-PB — Raspberry Pi 5 product brief (redirects to pip-assets RP-008348-DS-8): https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf
- RPI-PSU-doc — Raspberry Pi documentation, power supplies: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/power-supplies.adoc
- RPI-spec-doc — Raspberry Pi hardware documentation (specs, frequency management and thermal control, fan curve): https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
- RPI-blog-heat — "Heating and cooling Raspberry Pi 5": https://www.raspberrypi.com/news/heating-and-cooling-raspberry-pi-5/
- RPI-gpio-doc — GPIO on Raspberry Pi: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/gpio-on-raspberry-pi.adoc
- RPI-pad-doc — GPIO pad controls: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/gpio-pad-controls.adoc
- RPI-rtc-doc — Real Time Clock: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/raspberry-pi/rtc.adoc
- RP1-DS — RP1 Peripherals datasheet (GPIO function table, PWM §3.4, I2C §3.5): https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf
- OVL-README — Raspberry Pi device-tree overlays README (pps-gpio, pwm-pio, i2cN-pi5): https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/overlays/README
- CNX-pi5 — CNX Software Pi 5 review, power measurements (secondary): https://www.cnx-software.com/2023/11/05/raspberry-pi-5-review-raspberry-pi-os-bookworm-benchmarks-power-consumption/
- AIHAT-PB — Raspberry Pi AI HAT+ product brief (October 2024): https://datasheets.raspberrypi.com/ai-hat-plus/raspberry-pi-ai-hat-plus-product-brief.pdf
- RPI-ai-doc — Raspberry Pi documentation, AI HATs: https://www.raspberrypi.com/documentation/accessories/ai-hat-plus.html
- RPI-aihat2 — Raspberry Pi news, AI HAT+ 2: https://www.raspberrypi.com/news/introducing-the-raspberry-pi-ai-hat-plus-2-generative-ai-on-raspberry-pi-5/
- HAILO-8L — Hailo-8L product page: https://hailo.ai/products/ai-accelerators/hailo-8l-ai-accelerator-for-ai-light-applications/
- RPI-DFC — Raspberry Pi news, Dataflow Compiler now available: https://www.raspberrypi.com/news/raspberry-pi-ai-kit-update-dataflow-compiler-now-available/
- HMZ — Hailo Model Zoo README (branch/version support): https://github.com/hailo-ai/hailo_model_zoo
- HMZ-GS — Hailo Model Zoo getting started (system requirements, calibration): https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/GETTING_STARTED.rst
- ULTRA-hailo — Ultralytics Hailo export guide (secondary, compile constraints): https://docs.ultralytics.com/integrations/hailo
- ORT-pypi — onnxruntime on PyPI (aarch64 wheels, checked 2026-10-04): https://pypi.org/project/onnxruntime/

**Microcontrollers**

[ESP32-DS]: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
[WROOM32E-DS]: https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf
[WROOM32-NRND]: https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
[ESP32S3-DS]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
[S3WROOM1-DS]: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
[UNO-R3-DS]: https://docs.arduino.cc/resources/datasheets/A000066-datasheet.pdf
[UNO-R3-page]: https://docs.arduino.cc/hardware/uno-rev3/
[M328P-DS]: https://docs.arduino.cc/resources/datasheets/Atmel-42735-8-bit-AVR-Microcontroller-ATmega328-328P_Datasheet.pdf
[UNO-R4M-DS]: https://docs.arduino.cc/resources/datasheets/ABX00080-datasheet.pdf
[UNO-R4W-DS]: https://docs.arduino.cc/resources/datasheets/ABX00087-datasheet.pdf
[SERVO-h]: https://github.com/arduino-libraries/Servo/blob/master/src/Servo.h
[SERVO-avr]: https://github.com/arduino-libraries/Servo/blob/master/src/avr/Servo.cpp
[SERVO-ref]: https://docs.arduino.cc/libraries/servo/

- ESP32-DS — ESP32 Series Datasheet v5.3: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- WROOM32E-DS — ESP32-WROOM-32E/32UE Datasheet v2.1: https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf
- WROOM32-NRND — ESP32-WROOM-32 Datasheet v3.8 (NRND watermark): https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
- ESP32S3-DS — ESP32-S3 Series Datasheet v2.2: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- S3WROOM1-DS — ESP32-S3-WROOM-1/1U Datasheet v1.8: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- UNO-R3-DS — Arduino UNO R3 datasheet (A000066): https://docs.arduino.cc/resources/datasheets/A000066-datasheet.pdf
- UNO-R3-page — Arduino UNO R3 product docs: https://docs.arduino.cc/hardware/uno-rev3/
- M328P-DS — Microchip/Atmel ATmega328/P datasheet (42735B, 11/2016): https://docs.arduino.cc/resources/datasheets/Atmel-42735-8-bit-AVR-Microcontroller-ATmega328-328P_Datasheet.pdf
- UNO-R4M-DS — Arduino UNO R4 Minima datasheet (ABX00080): https://docs.arduino.cc/resources/datasheets/ABX00080-datasheet.pdf
- UNO-R4W-DS — Arduino UNO R4 WiFi datasheet (ABX00087): https://docs.arduino.cc/resources/datasheets/ABX00087-datasheet.pdf
- SERVO-h / SERVO-avr — Arduino Servo library source: https://github.com/arduino-libraries/Servo/blob/master/src/Servo.h , https://github.com/arduino-libraries/Servo/blob/master/src/avr/Servo.cpp
- SERVO-ref — Arduino Servo library reference (body did not render to the fetcher; statement from search index): https://docs.arduino.cc/libraries/servo/

**PWM driver**

[PCA9685-DS]: https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
[ADA-815]: https://www.adafruit.com/product/815
[ADA-PWM-hook]: https://learn.adafruit.com/16-channel-pwm-servo-driver/hooking-it-up
[ADA-PWM-h]: https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library/blob/master/Adafruit_PWMServoDriver.h

- PCA9685-DS — NXP PCA9685 product data sheet Rev. 4 (16 April 2015): https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
- ADA-815 — Adafruit 16-Channel 12-bit PWM/Servo Driver (product 815): https://www.adafruit.com/product/815
- ADA-PWM-hook — Adafruit learn guide, hooking it up: https://learn.adafruit.com/16-channel-pwm-servo-driver/hooking-it-up
- ADA-PWM-h — Adafruit PWM Servo Driver library header: https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library/blob/master/Adafruit_PWMServoDriver.h

**IMUs**

[OBS-brief]: https://theobsolescencebrief.com/imu-motion-sensor-obsolescence/
[ICM20948-DS]: https://cdn.sparkfun.com/assets/8/4/6/4/2/ds-000189-icm-20948-datasheet-2024.pdf
[BNO08x-DS]: https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf
[ADA-BNO085]: https://cdn-learn.adafruit.com/downloads/pdf/adafruit-9-dof-orientation-imu-fusion-breakout-bno085.pdf
[BMI270]: https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
[LSM6DSOX-DS]: https://www.st.com/resource/en/datasheet/lsm6dsox.pdf

- OBS-brief — The Obsolescence Brief, IMU obsolescence (secondary; cites TDK Product Center status fields): https://theobsolescencebrief.com/imu-motion-sensor-obsolescence/
- ICM20948-DS — TDK ICM-20948 datasheet DS-000189 rev 1.6 (SparkFun mirror; TDK copy: https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/ds-000189-icm-20948-v1.5.pdf): https://cdn.sparkfun.com/assets/8/4/6/4/2/ds-000189-icm-20948-datasheet-2024.pdf
- BNO08x-DS — CEVA BNO08X datasheet 1000-3927 v1.17: https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf
- ADA-BNO085 — Adafruit BNO085 learn guide (PDF): https://cdn-learn.adafruit.com/downloads/pdf/adafruit-9-dof-orientation-imu-fusion-breakout-bno085.pdf
- BMI270 — Bosch Sensortec BMI270 product page: https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
- LSM6DSOX-DS — ST LSM6DSOX datasheet (st.com timed out; figures from search index of this document): https://www.st.com/resource/en/datasheet/lsm6dsox.pdf

**GNSS / RTC / environment**

[M8-DS]: https://content.u-blox.com/sites/default/files/NEO-M8-FW3_DataSheet_UBX-15031086.pdf
[M9N-DS]: https://content.u-blox.com/sites/default/files/NEO-M9N-00B_DataSheet_UBX-19014285.pdf
[DS3231-DS]: https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf
[BME280-DS]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf

- M8-DS — u-blox NEO-M8 (FW3) data sheet UBX-15031086 R14: https://content.u-blox.com/sites/default/files/NEO-M8-FW3_DataSheet_UBX-15031086.pdf
- M9N-DS — u-blox NEO-M9N-00B data sheet UBX-19014285 R08: https://content.u-blox.com/sites/default/files/NEO-M9N-00B_DataSheet_UBX-19014285.pdf
- DS3231-DS — Maxim/ADI DS3231 datasheet 19-5170 Rev 7 (ADI URL timed out; read from mirror https://files.seeedstudio.com/wiki/High_Accuracy_Pi_RTC-DS3231/res/datasheet.pdf): https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf
- BME280-DS — Bosch BME280 datasheet BST-BME280-DS001-23 rev 1.23: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf

**Actuators**

[TP-MG996R]: https://www.towerpro.com.tw/product/mg996r/
[RCD-DS3218]: https://rcdrone.top/products/dsservo-ds3218-digital-servo
[HT-17HS4401S]: https://www.handsontec.com/dataspecs/17HS4401S.pdf
[OM-basics]: https://www.orientalmotor.com/stepper-motors/technology/stepper-motor-basics.html
[TMC2209-DS]: https://components101.com/sites/default/files/component_datasheet/TMC2209.pdf

- TP-MG996R — TowerPro MG996R official product page: https://www.towerpro.com.tw/product/mg996r/
- RCD-DS3218 — DSServo DS3218 reseller listing (secondary): https://rcdrone.top/products/dsservo-ds3218-digital-servo
- HT-17HS4401S — Handson Technology 17HS4401S data spec (secondary, distributor): https://www.handsontec.com/dataspecs/17HS4401S.pdf
- OM-basics — Oriental Motor, Stepper Motor Basics: https://www.orientalmotor.com/stepper-motors/technology/stepper-motor-basics.html
- TMC2209-DS — Trinamic TMC2209 datasheet Rev 1.03 (mirror; current ADI rev 1.09 at https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf was unreachable): https://components101.com/sites/default/files/component_datasheet/TMC2209.pdf

**Enclosure / climate**

[PM-PLA]: https://wiki.polymaker.com/polymaker-products/more-about-our-products/documents/technical-data-sheets/pla/polylite-tm-pla
[PM-PETG]: https://wiki.polymaker.com/polymaker-products/more-about-our-products/documents/technical-data-sheets/petg-pet/polylite-tm-petg
[PM-ASA]: https://wiki.polymaker.com/polymaker-products/more-about-our-products/documents/technical-data-sheets/abs-asa/polymaker-tm-asa
[PLEX-LT]: https://www.plexiglas.de/en/service/product-info/light-transmission
[PLEX-UV]: https://www.plexiglas.de/en/service/product-info/uv-resistance
[MAK-opt]: https://solutions.covestro.com/-/media/covestro/solution-center/brochures/pcs/brochures/optical-properties-of-makrolon-and-apec.pdf
[ASO-dew]: https://www.allskyoptics.com/store/product/allsky-camera-dew-heater-module
[DC-dew]: https://www.dewcontrol.com/product/dew-heater-module---all-sky-camera
[IPCODE]: https://en.wikipedia.org/wiki/IP_code
[LAPP-BS]: https://uk.farnell.com/lapp-kabel/53015800/cable-gland-spiral-tail-pg7/dp/1204185
[IMD-43295]: https://city.imd.gov.in/citywx/extreme/climat.php?id=43295
[WIKI-blr]: https://en.wikipedia.org/wiki/Bangalore

- PM-PLA / PM-PETG / PM-ASA — Polymaker technical data sheets (PolyLite PLA, PolyLite PETG, Polymaker ASA): https://wiki.polymaker.com/polymaker-products/more-about-our-products/documents/technical-data-sheets/
- PLEX-LT / PLEX-UV — Röhm PLEXIGLAS light transmission and UV resistance pages: https://www.plexiglas.de/en/service/product-info/light-transmission , https://www.plexiglas.de/en/service/product-info/uv-resistance
- MAK-opt — Covestro, "Optical properties of Makrolon and Apec": https://solutions.covestro.com/-/media/covestro/solution-center/brochures/pcs/brochures/optical-properties-of-makrolon-and-apec.pdf
- ASO-dew — AllSky Optics all-sky camera dew heater module: https://www.allskyoptics.com/store/product/allsky-camera-dew-heater-module
- DC-dew — Dew Control all-sky camera dew heater module: https://www.dewcontrol.com/product/dew-heater-module---all-sky-camera
- IPCODE — IP code summary of IEC 60529 (secondary; https://www.iec.ch/ip-ratings returned 403): https://en.wikipedia.org/wiki/IP_code
- LAPP-BS — LAPP SKINTOP BS PG7 (53015800) at Farnell (secondary, distributor; from listing): https://uk.farnell.com/lapp-kabel/53015800/cable-gland-spiral-tail-pg7/dp/1204185
- IMD-43295 — India Meteorological Department climatological table 1991–2020, station 43295: https://city.imd.gov.in/citywx/extreme/climat.php?id=43295
- WIKI-blr — Wikipedia "Bengaluru", climate table citing IMD normals 1991–2020 and extremes (secondary): https://en.wikipedia.org/wiki/Bangalore
