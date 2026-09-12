# Controller — Design & Incremental Build Guide

**Board:** ESP32-D0WD-V3, 38-pin WROOM-32 DevKit (`F4:2D:C9:71:7B:0C`)
**Peer:** robot ESP32 (`F4:2D:C9:71:0A:7C`)
**Components:** 1 analog joystick (2 axes + push), 4 buttons (one unassigned),
3 status LEDs, ESP-NOW link.
**Power:** 1S1P Li-ion 18650 → MT3608 boost → 5 V → DevKit VIN.
**Revised:** 13 August 2026. Supersedes the earlier draft of this file.
**Added to the repo:** 22 August 2026. Stage 3b marked implemented. Divider
values corrected to the **10 kΩ / 0.1 µF** actually fitted — confirmed on the
bench 22 Aug; this file previously specified 100 kΩ / 1 µF.

---

## 1. Power chain

```
  18650 ──── B+ ──┐
                  │   TP4056 module
  18650 ──── B- ──┘   (protected: TP4056 + DW01A + FS8205A)
                        │
                     OUT+ ──[ SWITCH ]──┬── MT3608 IN+ ──▶ 5.0 V OUT ──┬── VIN
                                        │                              └── 470 µF
                                        │   ↑ this node is the CELL, 3.0–4.2 V
                                        │
                                        └── R1 10k ───┬── R2 10k ─── GND
                                                      │
                                                  GPIO35 ──┬── 0.1 µF ── GND
                     OUT- ─────────────────────────────────────────────── GND
```

**The divider sits on the boost's INPUT node, in parallel with `MT3608 IN+` —
not on its output.** That distinction is the whole point: the MT3608's output is
a regulated 5 V whether the cell is full or nearly flat, so sensing there would
read a constant 5.00 V forever.

### Buy the *protected* TP4056

There are two module variants. The bare 4-pad board (`B+ B- OUT+ OUT-`, one IC)
charges only. The one you want has **two extra chips — a DW01A and an FS8205A**
— adding over-discharge, over-charge, over-current and short-circuit protection.
Cells damaged by deep discharge are also explicitly barred by the competition
rules, so the protected version is the right default. (If your 18650 is itself a
protected cell, with a PCB under the negative terminal, the two are redundant but
harmless.)

### Load goes on OUT+ / OUT-, never on B+ / B-

The FS8205A MOSFETs sit in the **ground path between `B-` and `OUT-`**, and the
DW01A opens them when the cell runs flat. Wire the load to `B+/B-` and you have
bypassed the over-discharge protection completely while everything still appears
to work. `OUT+/OUT-` is the only correct tap.

### Charge with the switch OFF

The TP4056 terminates charging when current falls below about C/10. A load
running off the same rail corrupts that measurement — the charger may never
terminate, or terminate early, and the cell gets trickle-cycled. Proper
load-sharing needs an extra P-FET arrangement.

You don't need one. Putting the **switch between `OUT+` and the MT3608 input**
means switching off disconnects the entire load, so charging always happens
against a clean, static cell. It also parks the MT3608's 1.6–2.2 mA PWM-mode
quiescent behind the switch, and the battery divider with it, so off-state drain
is essentially just the DW01A's microamps — months of shelf life.

The switch only ever carries ~200 mA with brief peaks near 500 mA; any small SPST
rated 1 A is ample.

### Charge current

The stock `RPROG` resistor is 1.2 kΩ = **1 A**, which is 0.33 C into a 3000 mAh
cell — safe, and about 3.5–4 hours for a full charge. The TP4056 is a *linear*
charger, so it dissipates (5 − V_cell) × I ≈ 1.3 W at 1 A and gets hot; it
thermally throttles rather than failing. Swapping `RPROG` to 2 kΩ (~580 mA) runs
cooler and is gentler on the cell if you can wait longer.

The module's own red/blue LEDs give you charge status for free — no firmware.

### Two protection layers, and which one actually matters

| Layer | Trips at | Role |
|---|---|---|
| Red LED warning (solid) | 3.30 V | tells you to swap — same instant as the refusal below |
| Firmware refuses to arm | 3.30 V | the real working limit |
| DW01A over-discharge | ~2.4 V | last-resort backstop |

Note the size of that gap. By the time the DW01A acts you are already deep into
cell-damaging territory — it is the "everything else failed" net, not a working
cutoff. **The firmware threshold is what protects the cell day to day.**

### Set the MT3608 to 5.0 V **before** it touches the ESP32

These modules ship with the multi-turn trimpot at an arbitrary position and can
output up to **28 V**. Power the module from the cell, put a multimeter on its
output, turn the pot until it reads 5.0 V, *then* wire it to VIN. Connecting
first and adjusting after destroys the board. The modules also have no reverse
polarity protection — check cell orientation twice.

### Add the bulk capacitor

The ESP32's WiFi transmit bursts pull 300 mA+ for a few hundred microseconds.
A boost converter responds to that more slowly than a battery would, and the
resulting dip can brown out the board. A 470 µF electrolytic across the 5 V rail
close to the DevKit absorbs it.

### Sense the **cell**, not the 5 V rail

This is the part that's easy to get wrong. The MT3608 outputs a regulated 5 V
whether the cell is at 4.2 V or 3.2 V, so measuring its output tells you nothing
about state of charge. The divider must tap the raw 18650, upstream of the boost.

**And this is precisely why the controller needs battery sense at all.** With a
boost converter in the chain there is no gradual decline to warn you — no dimming,
no sluggishness. The controller works perfectly and then stops, mid-run.

### Divider values

R1 = R2 = **10 kΩ**, giving a ratio of 2.0 and a maximum of **2.10 V** at the
ADC on a full cell. That is deliberate: the ESP32's ADC at 11 dB attenuation is
only well-behaved to roughly 2450 mV and clips above it, so a 2.1 V ceiling keeps
the whole cell range inside the linear region. Drain is 210 µA — irrelevant
against a 3000 mAh 18650 (3 mAh over a fifteen-hour session), but wire the divider
**after** the power switch anyway.

**Why 10 kΩ and not 100 kΩ.** The ratio is 2.0 either way, so the firmware cannot
tell the difference — `Batt::DIVIDER_RATIO` is 2.0 regardless. What changes is the
**source impedance**: R1∥R2 is **5 kΩ** at 10 kΩ, but **50 kΩ** at 100 kΩ. The
ESP32's ADC charges its sample-and-hold capacitor through that impedance during
every conversion, and the practical rule is to stay at or below about 10 kΩ.
5 kΩ is comfortably inside; 50 kΩ is not, and reads low by a few percent in a way
no `CAL_FACTOR` fixes cleanly, because the error varies with the sampling rate and
the voltage. A lower-impedance node also picks up less capacitively-coupled
switching noise from the MT3608.

### Why the reading is stable despite sitting next to a switching converter

The sense node is shared with the input of a 1.2 MHz boost converter, which draws
current in pulses — so there is real ripple on it. Three things suppress it:

**The cap is a low-pass filter, not just an ADC helper.** The divider's Thévenin
impedance is R1∥R2 = **5 kΩ**. With **0.1 µF** to GND that is an RC corner at
**318 Hz**. Against 1.2 MHz switching ripple that is roughly **71 dB of
attenuation** — the ripple is essentially gone by the time the ADC sees it.

**0.1 µF is enough, and 1 µF would be over-specified.** The capacitor only has to
reject what the software cannot: the EMA already has a ~3 s time constant and
handles everything slow, so the hardware filter only needs to kill the MT3608's
1.2 MHz ripple, and 71 dB does that comprehensively. Settling is τ = RC = **0.5 ms**,
which is why `Battery::begin()` can seed its filter from a single reading at boot
instead of ramping up from zero. A much larger capacitor would break that: it would
still be charging when the seed is taken, and the controller would report a false
CRITICAL on every power-on — the exact bug the seeding exists to prevent.

**Use a ceramic, not an electrolytic.** Electrolytics leak microamps, and across a
multi-kΩ source that becomes a voltage offset which drifts with temperature and
age — so `CAL_FACTOR` cannot absorb it.

**Oversampling handles ADC noise.** 16 reads averaged per sample, as in §4.

**A slow EMA handles load sag.** TX bursts pull 500 mA through the cell's ~50–100
mΩ internal resistance, dipping the node 25–50 mV. That is real, not noise, so
filter it in software over several seconds rather than trying to remove it in
hardware — and it is why the thresholds have hysteresis.

**Layout:** run the sense wire away from the MT3608's inductor, and return the
divider's bottom leg to the same ground point the DevKit uses. The switch's
contact drop (~10 mV at 200 mA) lands inside the measurement, which is harmless
and arguably more honest — it is what the converter actually receives.

*Bench check:* meter the cell while the firmware prints its reading, with the
radio idle and then transmitting. Agreement within ~30 mV across both means the
filtering is doing its job.

*Runtime sanity check:* ~120 mA average at 5 V ≈ 180 mA from the cell, so a
3000 mAh 18650 gives well over ten hours. Power is not a constraint here.

---

## 2. Pin map (locked)

| Function | GPIO | Header | Mode | Notes |
|---|---|---|---|---|
| Joystick VRx | **32** | 7 | `analogRead` | ADC1_CH4 |
| Joystick VRy | **33** | 8 | `analogRead` | ADC1_CH5 |
| Joystick SW | **13** | 15 | `INPUT_PULLUP` | hold 2 s = zero distance |
| Button 1 — ARM | **25** | 9 | `INPUT_PULLUP` | latching |
| Button 2 — *free* | **26** | 10 | — | unassigned |
| Button 3 — SPEED | **27** | 11 | `INPUT_PULLUP` | precision ↔ full |
| Button 4 — CAPTURE | **14** | 12 | `INPUT_PULLUP` | SAR image trigger |
| LED green — LINK | **18** | 30 | `OUTPUT` | + 330 Ω |
| LED yellow — ARMED | **19** | 31 | `OUTPUT` | + 330 Ω |
| LED red — ROBOT BATT | **23** | 37 | `OUTPUT` | + 330 Ω |
| Battery sense (cell) | **35** | 6 | `analogRead` | ADC1_CH7, input-only |
| Onboard LED | **2** | 24 | `OUTPUT` | heartbeat — **unconfirmed, see below** |
| *reserved* I2C | 21 / 22 | 33 / 36 | — | future OLED |

**Joystick `+5V` pin takes 3.3 V.** The silkscreen names the rail; it's two 10 kΩ
pots with no protection, so 5 V in means 5 V into a 3.3 V pin.

**This board has one LED, and it is a UART activity indicator, not GPIO2.**
Confirmed 13 Aug 2026: it flashes in lockstep with the serial print rate and
follows it when the rate is changed. Harmless, and useful as a "the board is
talking" tell. But it means **the usual blue GPIO2 LED may not be fitted**, so
the stage 3 heartbeat cannot assume it exists — verify with a bare
`pinMode(2, OUTPUT)` blink before relying on it. Fallbacks: brief flash on the
green LED between link updates, or GPIO26, which is still unassigned.

**Never use** GPIO6–11 (SPI flash) or GPIO12 (MTDI strapping — a pull-up here
stops the board booting).

---

## 3. LED behaviour

| LED | State | Meaning | Driven by |
|---|---|---|---|
| **Green** | Solid | Telemetry received < 300 ms ago | robot's telemetry packet |
| | Blink | MAC ACKs arriving, no telemetry — radio alive, firmware wedged | send callback vs telemetry |
| | Off | Silence ≥ 300 ms | timeout, not an event |
| **Yellow** | On | Robot reports armed | telemetry, **not** the local latch |
| | Off | Robot not armed, or link down | |
| **Red** | Off | Controller cell ≥ 3.50 V | local ADC, GPIO35 |
| | Blink | 3.30 – 3.50 V — swap soon | |
| | Solid | < 3.30 V — swap now; also refuses to arm | |
| **Onboard** | 1 Hz | Firmware alive | local |

Three rules behind this:

**Link state is a timeout, never an event.** Nothing arrives to tell you the link
died — that *is* the failure. `green = (millis() - lastTelemetryMs) < 300`.

**Green and yellow reflect the robot, not the controller.** The local arm latch is
intent; telemetry is reality. Armed-with-no-link is the dangerous combination, and
it's only visible if the two indicators are independent.

**Red is purely local.** It needs no radio and no robot, so it works on the bench
from stage 3 onward and stays meaningful even when the link is dead — which is
exactly when you most want to know whether your own cell is the reason.

*The robot's pack arrives over telemetry and has no indicator yet; that's part of
the robot-side work, still to be specified.*

---

## 4. Battery thresholds

### Controller — 1S Li-ion 18650 (red LED)

| Red LED | Cell voltage | Meaning |
|---|---|---|
| Off | ≥ 3.50 V | fine |
| Blink | 3.30 – 3.50 V | swap soon |
| Solid | < 3.30 V | swap now — also won't start a run it can't finish |

### Robot — TBD when its pack is chosen

Per-cell rules are the same: **3.50 V healthy / 3.30 V critical, coincident with
arm-refusal.**
Multiply by cell count once decided. The value arrives in the telemetry packet
from stage 5; how it gets surfaced on the controller is still open.

**Three caveats that matter more than the exact numbers:**

1. **Voltage is a poor gauge in the middle.** Li-ion is nearly flat from 3.7 to
   4.0 V. It's reliable at the ends — which is where you're using it.
2. **Hysteresis, or it strobes at the boundary.** Don't return to the healthier
   state until it climbs ~0.15 V back above the threshold.
3. **Filter for sag.** Motor current pulls the robot's pack down during
   acceleration. Average over several seconds and require consecutive low reads.

Use **`analogReadMilliVolts()`**, not `analogRead()` — it applies the per-chip
factory ADC calibration from eFuse. Raw counts scaled by 3.3/4095 can be ~10% off.
Then check once against a multimeter and fold the residual into a correction
factor; 5% resistors shift the ratio more than the ADC does.

---

## 5. Incremental build stages

Each stage is independently testable and leaves you with something that works.
Don't start the next until the exit test passes.

**Progress (30 Aug 2026)**

| Stage | State |
|---|---|
| 0 — power chain | **complete** — soldered to the board; boots on battery power, charges |
| 1 — joystick | **complete** — axes crossed in software, `PIN_AXIS_X` = GPIO33; `INVERT_X` flipped 30 Aug after bench testing found X backwards |
| 2 — buttons | **complete** — ARM needs a 2 s hold in both directions |
| — modular refactor | **complete** — `Software/controller-firmware/`, 10 files |
| 3a — LEDs + battery | **built, mid-test** — divider now reads correctly; bench test procedure written, not yet run |
| 3b — battery arm interlock | **implemented 22 Aug** — `V_ARM_MIN` 3.30 V, one branch in `control.cpp`; bench test procedure written, not yet run |
| 4 — ESP-NOW one-way | **bench-tested 12 Sep, passing** — `Software/shared/protocol.h` + `Software/controller-firmware/link.*` on the controller, `Software/tests/Arduino/robot-stage4-espnow-receiver/` as the robot-side exit-test sketch; green LED now reflects `Link::up()`. Flashed to both boards (controller `F4:2D:C9:71:7B:0C`, robot `F4:2D:C9:71:0A:7C`, identity confirmed via `esptool read-mac` before each flash); robot's serial showed a steady, CRC-valid packet stream (`rej=0`) with sequence numbers climbing cleanly. Required one fix first: `link.cpp`'s `onSent()` callback used the pre-3.1-core signature (`const uint8_t *mac`); Arduino-ESP32 core 3.3.x requires `const wifi_tx_info_t *`. `protocol.h` is still a literal copy in each sketch folder, not yet converted to the symlink the README's stage 4 note calls for. |
| 5 — telemetry back | not started |
| 6 — failsafe | not started |

**Stage 3a/3b bench test procedure**, written 30 Aug, not yet executed:
sweep the battery simulation (`v3.75` / `v3.40` / `v3.20`) and confirm the red
LED walks off → blink → solid with correct hysteresis (recovery only above
`V_LOW + HYSTERESIS` = 3.45 V and `V_GOOD + HYSTERESIS` = 3.65 V); verify the
raw (not filtered) serial reading against a multimeter within 0.05 V; for 3b,
confirm ARM refuses below 3.30 V with the right message, refuses off-centre
with the right message, battery takes priority over stick when both are true,
and — the one that matters most — disarm always succeeds regardless of
battery or stick state.

Stage 0 was deferred deliberately during stages 1–3: the battery chain added
nothing to them and only added variables, so that bench work ran on **USB
power**, joystick and LEDs fed from the board's own 3.3 V pin. Now soldered in
and confirmed: boots on battery, and charges normally.

*3a debugging note:* the first battery readings drifted from 0.79 V downward
while the divider metered a correct 1.93 V. Cause was **the cell and the board not
sharing a ground** — the ESP32 had no reference for the measurement, leaving
GPIO35 effectively floating and slowly discharging the 0.1 µF through pin leakage
at ~75 pA. Resolved.

> **Now that stage 0 is wired, never have USB and the MT3608 connected at
> the same time.** Many of these DevKits tie VBUS and VIN together with little or
> no isolation, and back-feeding a boost converter's output is a good way to lose
> the board. Habit to build now: unplug the boost before every upload.

| # | Build | Exit test | Watch out for |
|---|---|---|---|
| **0** | **Power chain.** Set MT3608 to 5.0 V *on the bench*. Wire cell → TP4056 `B+/B-` → `OUT+` → switch → boost → VIN + bulk cap. | Board boots on battery; switch kills it cleanly; `esp32-specs-check` runs; cell charges with switch off (red → blue LED). | Setting the pot after connecting. Reverse polarity. Tapping the load off `B+/B-`. |
| **1** | **Joystick.** Port `readAveraged()`, `normalise()`, `calibrate()` from `dual_joystick_s3.ino` to GPIO32/33/13. Serial only. | X/Y read −1.00…+1.00, centre auto-calibrates, deadzone has no jump at its edge. | Joystick on 5 V. `invertX/Y` are found by experiment, not reasoning. |
| **2** | **Buttons.** ARM latch (retarget `button-state.ino` to GPIO25), SPEED toggle (27), CAPTURE momentary (14), stick-hold-2 s zero (13). | All five inputs print correctly; long-press ARM forces off; held button at boot isn't read as a press. | GPIO26 stays free. Debounce every one, not just ARM. |
| **3** | **LEDs + battery sense — all local, no radio.** Wire 3 LEDs + 330 Ω. Divider + 0.1 µF on GPIO35; `analogReadMilliVolts()`; multimeter calibration. Red on its thresholds; yellow from the *local* latch for now; onboard heartbeat. | Reported voltage within 0.05 V of a multimeter across the range; red walks off → blink → solid as the cell drains. | Tap the cell, **not** the 5 V rail. Temporary: yellow moves to robot-reported in stage 5. |
| **4** | **ESP-NOW one-way.** Controller → robot @ 50 Hz. Minimal receiver on the robot that prints the packet. Green from send-callback status. | Robot prints packets; green tracks robot power. | Both ends `WIFI_STA` + `WiFi.disconnect()`, **same fixed channel**. Unicast, not broadcast — broadcast has no ACK. |
| **5** | **Telemetry back.** Robot → controller @ 10 Hz: distance, pack voltage, armed, status. Green switches to 300 ms telemetry timeout; yellow to robot-reported. | Power off the robot → green dies within 300 ms. Yellow follows the robot, not your latch. | MAC ACK ≠ application alive. Keep both layers; blink green for the gap. |
| **6** | **Failsafe.** 250 ms no packet → motors stop. Hold arm 2 s, then disarm. On reconnect resume at **zero throttle**. Refuse to arm below critical. | Cut controller power mid-drive → robot stops immediately, disarms after 2 s, doesn't lurch on return. | Resuming at the last command is the dangerous default. |

**Control packet (stage 4)** — ~10 bytes, ESP-NOW allows 250:

| Field | Type | Purpose |
|---|---|---|
| `magic` | `uint8` | reject strays |
| `seq` | `uint16` | loss / reorder detection |
| `axisX`, `axisY` | `int16` | −1000…+1000, centred and deadbanded |
| `buttons` | `uint8` | bitmask |
| `armed` | `uint8` | arm latch |
| `crc` | `uint8` | integrity |

**Telemetry packet (stage 5):** `distance_cm`, `battery_mV`, `armed`, `status`,
`seq`.

---

## 6. Still open

- Robot pack chemistry and cell count — sets the red LED thresholds.
- Button 2 (GPIO26) unassigned. Note: once armed, this pin can never be an
  analog input either — see the ESP-NOW/ADC2 note in
  `esp32_devkit_38pin_pinout.md`.
- Robot ↔ S3 CAM link is **UART** (decided); pins and framing not yet specified.
  The competition requires encoder distance and image to travel together.
- `Software/shared/protocol.h` is duplicated by hand into
  `controller-firmware/` and the robot's test sketch folder rather than
  symlinked, per the README's stage 4 note — convert both to real symlinks
  before editing the packet format again, or the two copies will drift.
- ESP-NOW channel (`Protocol::CHANNEL = 1`) is an arbitrary first choice, not
  a measured or previously-decided value — fine unless venue RF is congested,
  in which case change it on both ends together.
- Robot-side GPIO pin map for the TB6612FNG driver, encoders and any
  endstops — not decided. Whatever it becomes must keep every analog line on
  ADC1 (GPIO32/33/34/35/36/39), since the robot will run the same ESP-NOW
  radio path as the controller.
