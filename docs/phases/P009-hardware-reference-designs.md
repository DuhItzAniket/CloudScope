# P009 — Hardware reference designs

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
Define verified bills of materials, wiring, power and thermal layouts for every deployment configuration (D1–D4), based on manufacturer data rather than assumptions.

## Requirements covered
NFR-SAFE-03 (separate servo supply), FR-CTL-05/06, FR-FW-01/02, FR-SAF-05 (fail-safe e-stop input), FR-PLT-01 (Pi platform); SRS amended to v1.3.

## Design notes
- Evidence: `docs/research/P009_hardware_component_facts.md` (manufacturer datasheets first; secondary sources and unverified items flagged; 66 references).
- Designs: `docs/hardware/reference_designs.md` — principles H1–H8, R1 servo pan-tilt, R2 precision stepper pan-tilt, Pi 5 box/native GPIO tier, R3 lite controller, thermal plan, decisions HW-1…HW-8, open items.
- **Facts that changed the design:**
  - TowerPro states the MG996R travels only 0–159° and is rated 0–55 °C → excluded from the head; 270° digital servos or steppers instead.
  - Raspberry Pi AI Kit is discontinued; the AI HAT+ is rated 0–50 °C ambient → box-mounted only; HEF compile step on x86-64 Linux.
  - Pi 5 USB budget is 600 mA without a 5 V/5 A PD supply → official 27 W PSU whenever a disk is attached.
  - `pps-gpio` defaults to GPIO18, a hardware PWM pin → PPS on GPIO17.
  - ESP32-WROOM-32 is NRND; ESP32-S3 R8/R16V parts are rated to 65 °C → ESP32-S3 N8/N16/N16R2 as the reference controller.
  - MPU-6050 reported obsolete, ICM-20948 EOL with 1.8 V I/O; the BNO08x library does not fit an Uno R3 → BNO085 reference IMU; Uno R4 Minima as the lite reference.
  - B0268 full resolution is MJPG only and the lens is strongly distorted → recorded as SRS constraint C6.
- **Split head/box architecture** (HW-1) answers the Bengaluru thermal envelope (April mean daily maximum 34.1 °C, record 39.2 °C, plus solar gain).

## Work log
1. Commissioned and reviewed the component-facts document (camera, Pi 5 and AI HATs, ESP32/Uno, PCA9685, IMUs, GNSS/RTC/BME280, servos vs steppers, enclosure materials, Bengaluru climate, power budget).
2. Wrote the reference designs with two Mermaid diagrams (system blocks, power distribution) and pin maps (ESP32-S3 and Pi 5 marked provisional).
3. Amended `docs/PLAN.md` (platform table, ADR-004 row, P009, P052, P054, P066, shopping list) and the SRS (FR-FW-02, constraints C6/C7).

## Verification
- Evidence document: 66 reference tags used, 66 defined, none dangling (script check).
- Both new diagrams render with Mermaid 11 (`reference_designs.md#0` and `#1`: OK); the six architecture diagrams were re-checked in the same run (OK).
- Every component in the BOMs appears in the evidence document with a source, or is explicitly listed under open items.

## Exit criteria
- [x] Diagrams and BOM per tier committed (`docs/hardware/`; the plan's `hardware/` folder is reserved for CAD and wiring files from P094/P095).
- [x] Power budget with a separate servo rail.

## Safety & failure-mode notes
- Servo rail sized for simultaneous stall (≥5 A at 6 V) and kept off the Pi/MCU 5 V pins.
- E-stop is normally-closed (a cut wire stops motion); PCA9685 OE provides a hardware "all outputs off".
- Low-temperature-rated parts (AI HAT+ 50 °C, MG996R 55 °C) are banned from the sun-exposed head.

## Deviations & next phase
- Designs live in `docs/hardware/` rather than `hardware/` (documentation vs fabrication files).
- Pin maps are provisional until the boards are in hand (P050, P055).
- Next: **P010 — Gate R (requirements and architecture review)**.
