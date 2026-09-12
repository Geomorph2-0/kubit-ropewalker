# ESP32 DevKit 38-pin — Pinout and Project Assignments

**Board:** ESP32-D0WD-V3, ESP-WROOM-32 module, 38-pin DevKit, CP2102 bridge.
Applies to both classic ESP32s: controller `F4:2D:C9:71:7B:0C` and robot
`F4:2D:C9:71:0A:7C`.

Header numbering follows the standard DIYables-style diagram: **pin 1 top-left,
counting down the left column to 19, then pin 20 bottom-right counting up the
right column to 38.**

*(Reference diagrams for this board usually show a USB-C connector; ours is
micro-USB. The header layout is identical — only the socket differs.)*

---

## Left column

| Pin | GPIO | Alt functions | Project use |
|---|---|---|---|
| 1 | — | **3.3 V out** | joystick VCC, LED pull-ups if needed |
| 2 | — | EN / RESET | — |
| 3 | 36 | ADC1_CH0, **input-only** | free — spare ADC1 |
| 4 | 39 | ADC1_CH3, **input-only** | free — spare ADC1 |
| 5 | 34 | ADC1_CH6, **input-only** | free — spare ADC1 |
| 6 | **35** | ADC1_CH7, **input-only** | **BATTERY SENSE** (2:1 divider) |
| 7 | **32** | ADC1_CH4, TOUCH9 | **JOYSTICK Y** ← module VRx |
| 8 | **33** | ADC1_CH5, TOUCH8 | **JOYSTICK X** ← module VRy |
| 9 | **25** | ADC2_CH8, DAC1 | **ARM** button |
| 10 | **26** | ADC2_CH9, DAC2 | **free** — deliberately unassigned |
| 11 | **27** | ADC2_CH7, TOUCH7 | **SPEED** button |
| 12 | **14** | ADC2_CH6, TOUCH6 | **CAPTURE** button |
| 13 | 12 | ADC2_CH5, TOUCH5, **MTDI strapping** | ⛔ **NEVER USE** — see below |
| 14 | — | **GND** | common ground |
| 15 | **13** | ADC2_CH4, TOUCH4 | **STICK SW** (joystick push) |
| 16 | 9 | FLASH D2, RX1 | ⛔ SPI flash |
| 17 | 10 | FLASH D3, TX1 | ⛔ SPI flash |
| 18 | 11 | FLASH CMD | ⛔ SPI flash |
| 19 | — | **Vin 5 V** | MT3608 output (stage 0) |

## Right column

| Pin | GPIO | Alt functions | Project use |
|---|---|---|---|
| 20 | 6 | FLASH CK | ⛔ SPI flash |
| 21 | 7 | FLASH D0 | ⛔ SPI flash |
| 22 | 8 | FLASH D1 | ⛔ SPI flash |
| 23 | 15 | ADC2_CH3, TOUCH3, **strapping** | avoid as input |
| 24 | 2 | ADC2_CH2, TOUCH2, **strapping** | usually the onboard LED — **not fitted on our board** |
| 25 | 0 | ADC2_CH1, TOUCH1, **strapping**, BOOT | avoid |
| 26 | 4 | ADC2_CH0, TOUCH0 | free |
| 27 | 16 | RX2 | free |
| 28 | 17 | TX2 | free — **candidate for the robot↔S3 UART link** |
| 29 | 5 | SPI SS, **strapping** | avoid as input |
| 30 | **18** | SPI SCK | **LED GREEN** |
| 31 | **19** | SPI MISO | **LED YELLOW** |
| 32 | — | **GND** | common ground |
| 33 | 21 | I2C SDA | *reserved* — future OLED |
| 34 | 3 | **RX0** | ⛔ keep free — serial monitor (see the temporary S3-debug-tap exception in `controller-firmware.ino`) |
| 35 | 1 | **TX0** | ⛔ keep free — serial monitor (see the temporary S3-debug-tap exception in `controller-firmware.ino`) |
| 36 | 22 | I2C SCL | *reserved* — future OLED |
| 37 | **23** | SPI MOSI | **LED RED** |
| 38 | — | **GND** | common ground |

---

## The four traps this board hides

**1. GPIO 6–11 are the SPI flash bus.** Header pins 16, 17, 18, 20, 21, 22. They
are broken out on the 38-pin board and labelled `FLASH …` on the diagrams, which
reads like a feature. Using them crashes or bricks the board. Six of the
thirty-eight pins are effectively decorative.

**2. GPIO 12 will stop the board booting.** It is the MTDI strapping pin, and it
must be **LOW at reset**. Anything that pulls it high — including
`INPUT_PULLUP` for a button — sets the flash regulator to 1.8 V and the chip
never starts. It looks exactly like a dead board.

**3. ADC2 dies when the radio starts.** ADC2 belongs to the WiFi driver, and
`analogRead()` on an ADC2 pin fails **silently** once WiFi or ESP-NOW is up —
garbage values, no error. Every analog input must be on **ADC1**, which on this
module means GPIO **32, 33, 34, 35, 36, 39** only. See "ESP-NOW and ADC2" below
for the complete list of which GPIOs that rules out, now that stage 4 actually
turns the radio on.

Digital use of ADC2 pins is completely fine, which is why ARM/SPEED/CAPTURE sit
on GPIO 25/27/14.

**4. GPIO 34/35/36/39 are input-only with no internal pull-up.** Useless for
buttons, ideal for analog sense lines — which is exactly why battery sense is on
GPIO35.

*ADC1 channels 1 and 2 (GPIO 37 and 38) exist on the silicon but are not bonded
out on WROOM modules. That is why the diagram jumps from ADC0 to ADC3, and why
only six of ADC1's eight channels are usable.*

---

## Controller assignment summary

| Function | GPIO | Header |
|---|---|---|
| Joystick X (module VRy) | 33 | 8 |
| Joystick Y (module VRx) | 32 | 7 |
| Stick SW | 13 | 15 |
| ARM | 25 | 9 |
| *free* | 26 | 10 |
| SPEED | 27 | 11 |
| CAPTURE | 14 | 12 |
| LED green | 18 | 30 |
| LED yellow | 19 | 31 |
| LED red | 23 | 37 |
| Battery sense | 35 | 6 |

Joystick VCC on **3.3 V (pin 1)**, never 5 V — the module is two bare 10 kΩ pots
with no clamp, so 5 V in means 5 V into a 3.3 V-only input.

Robot-side assignments are not yet decided beyond the UART link to the S3 CAM
board.

---

## ESP-NOW and ADC2 (applies once stage 4's radio is running)

Stage 4 (`Software/controller-firmware/link.*`, `Software/shared/protocol.h`)
calls `WiFi.mode(WIFI_STA)` and brings up ESP-NOW in `Link::begin()`, which
means trap #3 above stops being theoretical the moment that runs. Every ADC2
channel on this module goes bad for `analogRead()` from that point on, for
the rest of the sketch's run — not flaky, not degraded, just silently wrong.
This module bonds out all ten ADC2 channels; here's the complete list, so
"avoid ADC2" has a concrete GPIO set behind it instead of a rule to remember:

| ADC2 channel | GPIO | Header pin | Controller status |
|---|---|---|---|
| CH0 | 4 | 26 | free |
| CH1 | 0 | 25 | avoid anyway — strapping/BOOT |
| CH2 | 2 | 24 | onboard LED (not fitted) |
| CH3 | 15 | 23 | avoid anyway — strapping |
| CH4 | 13 | 15 | **STICK SW** — digital, unaffected |
| CH5 | 12 | 13 | never use anyway — MTDI strapping |
| CH6 | 14 | 12 | **CAPTURE** — digital, unaffected |
| CH7 | 27 | 11 | **SPEED** — digital, unaffected |
| CH8 | 25 | 9 | **ARM** — digital, unaffected |
| CH9 | 26 | 10 | **free** — deliberately unassigned |

**Controller: nothing currently breaks.** ARM/SPEED/CAPTURE/STICK were
already read with `digitalRead()`, which trap #3 explicitly says stays fine
on ADC2, and both real analog inputs (joystick X/Y) plus battery sense were
already placed on ADC1. The only consequence is forward-looking: GPIO26 —
the deliberately-free button 2 pin — can **never** be read with
`analogRead()` once `Link::begin()` has run, only `digitalRead()`. If a
future feature wants a second analog input (a pot, a second joystick axis),
it has to land on a free ADC1 pin — GPIO34, 36 or 39 — not GPIO26.

**Robot: this is the constraint the still-undecided pin map has to design
around, not a warning to revisit later.** The robot will run this same
`link.*` code path (receiving is planned for stage 4/5, and it needs its own
WiFi STA + ESP-NOW init regardless), so the same ten GPIOs above are off
limits for `analogRead()` there too. In practice:

- **DRV8833 driver pins** (`AIN1`/`AIN2`/`BIN1`/`BIN2`/`nSLEEP`, plus `nFAULT`
  if wired as a status input) and **encoder pins** are all digital or PWM, so
  they're fine on ADC2 GPIOs — same as the controller's buttons.
- **If the robot gets its own battery divider** — section 4 of
  `controller_plan.md` already flags the robot pack's threshold as "TBD when
  its pack is chosen" — that divider **must** land on a free ADC1 pin
  (GPIO34, 36 or 39; GPIO32/33/35 are taken on the controller but still free
  on the robot until its own pin map is drawn up), never on an ADC2 GPIO,
  for exactly the reason the controller's own battery sense is on GPIO35 and
  not GPIO26.
- Any other analog sensor considered for the robot (current sense, IR
  reflectance, etc.) needs the same ADC1-only rule applied before its pin is
  chosen, not after.
