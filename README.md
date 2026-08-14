# kubit-ropewalker

A rope-walking crawler robot for **SST Makerspace 3.0 — The Rope Runner
Challenge** (Competition Brief V2, October 2026).

The robot hangs from a rope strung 25 cm above the floor and drives along it.
Two scored activities:

- **Activity One — Swiftplay.** Traverse the rope as fast as possible, handling
  obstacles without falling.
- **Activity Two — Search & Rescue.** While traversing, use a downward-facing
  camera to spot printed "victim" markers on the floor, pair each image with a
  distance derived from wheel encoders, and upload both to the competition
  server. Healthy-person markers are mixed in as distractors, so *not* reporting
  those is part of the score.

Full requirements, extracted verbatim from the brief with contradictions
flagged, are in
[docs/competition/SAR_requirements_extract.md](docs/competition/SAR_requirements_extract.md).

## Hardware

Three microcontrollers, all characterised on the bench — measured figures, not
datasheet claims.

| Role | Board | MAC (WiFi STA) | Notes |
|---|---|---|---|
| Ground controller | ESP32-D0WD-V3 DevKit, 38-pin | `F4:2D:C9:71:7B:0C` | Joystick + buttons |
| Robot motion | ESP32-D0WD-V3 DevKit, 38-pin | `F4:2D:C9:71:0A:7C` | Drive, encoders |
| Camera / upload | Freenove ESP32-S3-WROOM CAM, N16R8 | `D0:CF:13:00:2C:E0` | 16 MB flash, 8 MB PSRAM |

Motor driver is a TB6612FNG. Board specs and the bring-up logs live in
[docs/hardware/](docs/hardware/).

## Layout

```
docs/
  competition/   both competition briefs + the SAR requirements extract
  hardware/      board spec sheets, bring-up logs, motor driver datasheet
  vendor/        Freenove SDK — gitignored, ~370 MB, redownloadable
Software/
  controller-firmware/   the ground controller (see below)
  tests/Arduino/         bring-up sketches, one sketch per folder
  tools/ds4-monitor/     host-side Linux C, not firmware
```

Everything under `Software/tests/Arduino/` is an Arduino sketch, with no
exceptions — anything host-side belongs in `Software/tools/`. The
`Sketch_04.1_*` / `Sketch_07.1_*` names are deliberate: they match the Freenove
tutorial numbering, so don't rename them.

## Controller firmware

**Current stage: 2 (complete, modular rebuild).** Joystick and buttons work;
nothing transmits yet.

Built for **Arduino IDE**. Open
[Software/controller-firmware/controller-firmware.ino](Software/controller-firmware/controller-firmware.ino),
select the classic ESP32 DevKit board, and Verify. Serial monitor at **115200**.

| File | Responsibility |
|---|---|
| [config.h](Software/controller-firmware/config.h) | Every GPIO and tuning constant. Nothing else hardcodes a pin. |
| [joystick.*](Software/controller-firmware/joystick.cpp) | Boot calibration, 8× oversampling, deadzone, normalisation |
| [buttons.*](Software/controller-firmware/buttons.cpp) | Debounce, press/release/long-hold edges |
| [control.*](Software/controller-firmware/control.cpp) | Arm latch, speed mode, transmitted intent — the policy layer |
| [ui.*](Software/controller-firmware/ui.cpp) | 10 Hz status line, serial commands |

Dependencies flow one way: `.ino` → `control` → `{joystick, buttons}`, with `ui`
reading everything. No module calls another sideways.

### Wiring

| Signal | GPIO | Notes |
|---|---|---|
| Joystick X | 33 | module VRy — axes crossed in software |
| Joystick Y | 32 | module VRx |
| Stick SW | 13 | switch → GND; long-hold zeroes distance |
| ARM | 25 | switch → GND; hold 2 s to arm or disarm |
| SPEED | 27 | switch → GND; toggles precision / full |
| CAPTURE | 14 | switch → GND |

Module VCC must be on **3.3 V**. Both axes are on **ADC1** on purpose — ADC2
reads fail silently once the WiFi radio starts, so an ADC2 pin would work on the
bench and die the moment ESP-NOW comes up at stage 4.

### Behaviour

Boots **DISARMED** in **PRECISION** mode. Arming is refused while the stick is
off-centre, and transmitted values are forced to hard zero while disarmed — so
no single bug on either side can drive the motors alone. Press `c` in the serial
monitor to recalibrate the stick centre.

## Roadmap

- **Stage 3** — status LEDs (GPIO 18/19/23) and battery sense (GPIO 35)
- **Stage 4** — ESP-NOW link: `protocol.h` + `link.*`, no changes to existing modules
- **Stage 6** — failsafe rules, which live in `control.*` so the whole safety story stays on one screen

**Note for stage 4.** Arduino IDE copies the sketch folder to a build directory
at compile time, so `#include "../shared/protocol.h"` will *not* work. Plan is to
keep the canonical file at `Software/shared/protocol.h` and place a relative
symlink to it inside each sketch folder that needs it — git stores symlinks
natively and the IDE follows them on Linux.

## Building the host tool

`ds4-monitor` is a Linux DualShock 4 input monitor, not firmware. It needs SDL2
(`sudo apt install libsdl2-dev`). The compiled binary is gitignored; rebuild it
with:

```bash
gcc -O2 -o Software/tools/ds4-monitor/ds4-monitor Software/tools/ds4-monitor/ds4-monitor.c -lSDL2 -lm
```
