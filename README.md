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

Motor driver is a DRV8833. Board specs and the bring-up logs live in
[docs/hardware/](docs/hardware/).

## Layout

```
docs/
  controller_plan.md   controller design & staged build guide — power chain,
                       pin map, LED and battery rules, stages 0-6
  competition/   both competition briefs + the SAR requirements extract
  hardware/      board spec sheets, bring-up logs, motor driver datasheet
  vendor/        Freenove SDK — gitignored, ~370 MB, redownloadable
Software/
  shared/protocol.h      ESP-NOW wire format, controller <-> robot (stage 4)
  controller-firmware/   the ground controller (see below)
  tests/Arduino/         bring-up sketches, one sketch per folder, including
                         robot-stage4-espnow-receiver, the stage 4 exit-test
                         sketch for the robot
  tools/ds4-monitor/     host-side Linux C, not firmware
```

Everything under `Software/tests/Arduino/` is an Arduino sketch, with no
exceptions — anything host-side belongs in `Software/tools/`. The
`Sketch_04.1_*` / `Sketch_07.1_*` names are deliberate: they match the Freenove
tutorial numbering, so don't rename them.

## Controller firmware

**Current stage: 4 (ESP-NOW link).** Joystick, buttons, status LEDs and
battery sensing all work; the arm latch refuses to close on a flat cell; the
controller now sends a control packet to the robot over ESP-NOW at 50 Hz.
**Stage 4 bench-tested and passing** (2026-09-12): flashed to both boards and
confirmed the robot receiving a steady, CRC-valid packet stream from the
controller (`rej=0`, sequence numbers climbing cleanly). This also required a
one-line fix in `link.cpp` — the Arduino-ESP32 3.1+ core changed the ESP-NOW
send-callback signature from `(const uint8_t *mac, ...)` to
`(const wifi_tx_info_t *, ...)`. See
[docs/controller_plan.md](docs/controller_plan.md) for the stage 3a/3b bench
test procedures, which have not yet been run.

The design rationale behind all of it — why the divider taps the cell and not the
5 V rail, why each stage stops where it does, what each LED is allowed to mean —
is in [docs/controller_plan.md](docs/controller_plan.md).

Built for **Arduino IDE**. Open
[Software/controller-firmware/controller-firmware.ino](Software/controller-firmware/controller-firmware.ino),
select the classic ESP32 DevKit board, and Verify. Serial monitor at **115200**.

| File | Responsibility |
|---|---|
| [config.h](Software/controller-firmware/config.h) | Every GPIO and tuning constant. Nothing else hardcodes a pin. |
| [joystick.*](Software/controller-firmware/joystick.cpp) | Boot calibration, 8× oversampling, deadzone, normalisation |
| [buttons.*](Software/controller-firmware/buttons.cpp) | Debounce, press/release/long-hold edges |
| [battery.*](Software/controller-firmware/battery.cpp) | Divider read, filtering, level with hysteresis, simulation |
| [control.*](Software/controller-firmware/control.cpp) | Arm latch, speed mode, transmitted intent — the policy layer |
| [leds.*](Software/controller-firmware/leds.cpp) | Mode-driven LED animation and power-on self test |
| [ui.*](Software/controller-firmware/ui.cpp) | 10 Hz status line, serial commands, LED mapping |
| [link.*](Software/controller-firmware/link.cpp) | Stage 4 — builds and sends the ESP-NOW control packet at 50 Hz |
| [protocol.h](Software/controller-firmware/protocol.h) | Wire format shared with the robot. **Currently a hand-copied duplicate of `Software/shared/protocol.h`, not a symlink — see the note below.** |

Dependencies flow one way: `.ino` → `control` → `{joystick, buttons, battery}`,
with `leds` and `link` driven by `ui`/`control` and `ui` reading everything
else. No module calls another sideways.

### Wiring

| Signal | GPIO | Notes |
|---|---|---|
| Joystick X | 33 | module VRy — axes crossed in software |
| Joystick Y | 32 | module VRx |
| Stick SW | 13 | switch → GND; long-hold zeroes distance |
| ARM | 25 | switch → GND; hold 2 s to arm or disarm |
| SPEED | 27 | switch → GND; toggles precision / full |
| CAPTURE | 14 | switch → GND |
| LED green | 18 | heartbeat pulse |
| LED yellow | 19 | armed indicator |
| LED red | 23 | controller cell level (off / blink / on) |
| Battery sense | 35 | 2:1 divider off the controller's own 1S Li-ion cell |

Module VCC must be on **3.3 V**. Both axes are on **ADC1** on purpose — ADC2
reads fail silently once the WiFi radio starts, which is no longer a future
concern now that `Link::begin()` actually brings ESP-NOW up at stage 4. See
the "ESP-NOW and ADC2" section in
[docs/hardware/esp32_devkit_38pin_pinout.md](docs/hardware/esp32_devkit_38pin_pinout.md)
for the full list of GPIOs that rules out on both boards, including the robot's
still-undecided pin map.

### Behaviour

Boots **DISARMED** in **PRECISION** mode. Arming is refused while the stick is
off-centre or while the controller's own cell is below `Batt::V_ARM_MIN`
(3.30 V), and transmitted values are forced to hard zero while disarmed — so
no single bug on either side can drive the motors alone. Disarming is never
gated on anything. Press `c` in the serial monitor to recalibrate the stick
centre, `t` to re-run the LED self test, or `v3.45` / `v` to simulate or clear
a battery voltage for testing.

## Roadmap

- **Stage 4** — done in code, not yet bench-tested. `protocol.h` + `link.*`,
  no changes to the existing input/policy modules (`joystick.*`, `buttons.*`,
  `battery.*`, `control.*`); `ui.cpp`'s green LED now reflects `Link::up()`.
- **Stage 5** — telemetry back (robot → controller); yellow LED moves from
  the local arm latch to the robot's reported state.
- **Stage 6** — failsafe rules, which live in `control.*` so the whole safety story stays on one screen

**Stage 4's symlink is not done yet.** Arduino IDE copies the sketch folder to
a build directory at compile time, so `#include "../shared/protocol.h"` does
*not* work. The plan is still to keep the canonical file at
`Software/shared/protocol.h` and place a relative symlink to it inside each
sketch folder that needs it (git stores symlinks natively and the IDE follows
them on Linux) — but right now `controller-firmware/protocol.h` and
`tests/Arduino/robot-stage4-espnow-receiver/protocol.h` are hand-made copies
of the canonical file, not symlinks. Convert both before changing the packet
format, or the two copies will drift silently:

```bash
cd Software/controller-firmware && rm protocol.h && ln -s ../shared/protocol.h protocol.h
cd Software/tests/Arduino/robot-stage4-espnow-receiver && rm protocol.h && ln -s ../../../shared/protocol.h protocol.h
```

## Building the host tool

`ds4-monitor` is a Linux DualShock 4 input monitor, not firmware. It needs SDL2
(`sudo apt install libsdl2-dev`). The compiled binary is gitignored; rebuild it
with:

```bash
gcc -O2 -o Software/tools/ds4-monitor/ds4-monitor Software/tools/ds4-monitor/ds4-monitor.c -lSDL2 -lm
```
