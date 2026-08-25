/*
 * controller-firmware.ino — rope-walker ground controller.
 *
 * Stage 3b: the battery arming interlock, on top of 3a's indicators and sensing.
 *
 * 3a was purely additive — joystick.*, buttons.* and control.* were untouched.
 * 3b is the first change to the policy layer since stage 2: control.cpp gains a
 * single branch that refuses to close the arm latch below Batt::V_ARM_MIN. That
 * branch is the entire delta, which is why it was worth separating from 3a.
 *
 * TARGET BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit.
 *               Controller MAC F4:2D:C9:71:7B:0C.
 *
 * FILES
 *   config.h      every pin and tuning constant — the only place GPIOs appear
 *   joystick.*    calibration, oversampling, deadzone, normalisation
 *   buttons.*     debounce, press/release/long-hold edges
 *   battery.*     divider read, filtering, level with hysteresis, simulation
 *   control.*     arm latch, speed mode, transmitted intent — the policy layer
 *   leds.*        mode-driven LED animation and power-on self test
 *   ui.*          status line, serial commands, LED mapping
 *
 * Dependencies flow one way: this file -> control -> {joystick, buttons}, and
 * ui reads everything. No module calls another sideways, so nothing here has a
 * circular include and each module can be reasoned about alone.
 *
 * Stage 4 adds protocol.h and link.* without touching any of the above.
 *
 * WIRING
 *
 *   Signal        GPIO    Header   Wiring
 *   ------        ----    ------   ------
 *   Joystick X    33      8        module VRy  (axes crossed in software)
 *   Joystick Y    32      7        module VRx
 *   Stick SW      13      15       switch -> GND
 *   ARM           25      9        switch -> GND
 *   (free)        26      10       nothing fitted
 *   SPEED         27      11       switch -> GND
 *   CAPTURE       14      12       switch -> GND
 *   LED green     18      30       GPIO -> R -> anode, cathode -> GND
 *   LED yellow    19      31       "
 *   LED red       23      37       "
 *   Battery sense 35      6        cell+ -> 100k -> node -> 100k -> GND,
 *                                  node -> GPIO35, node -> 1uF -> GND
 *
 * Size the LED resistors from each LED's MEASURED forward voltage. A modern
 * InGaN green sits at 3.0-3.4 V and will barely light from a 3.3 V pin through
 * 330 ohm, while red at 1.9 V is comfortable on the same value.
 *
 * Serial Monitor line ending must be Newline — commands are read as whole lines
 * so that 'v3.45' can carry an argument.
 */

#include <Arduino.h>

#include "config.h"
#include "joystick.h"
#include "buttons.h"
#include "battery.h"
#include "control.h"
#include "leds.h"
#include "ui.h"

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  Buttons::begin();    // first: banner reports anything held at boot
  UI::banner();
  Leds::begin();       // pin modes, then the ~900 ms self test
  Joystick::begin();   // configures the ADC, then calibrates and prints
  Battery::begin();    // seeds its filter from one real reading
  Control::begin();
  UI::ready();
}

void loop() {
  const uint32_t now = millis();

  UI::handleInput();

  /* Inputs, then policy, then output. That order is the architecture — Control
   * must see this pass's stick position before deciding whether arming is
   * allowed, and UI must run after it to display the decision just made.
   *
   * Battery must run before Control, because Control's refuse-to-arm rule reads
   * this pass's filtered cell voltage. 3a already had this order for exactly
   * that reason, so 3b needed no reordering — only the new branch.
   *
   * Leds runs last and every pass: UI sets modes on its 100 ms tick, but the
   * animation needs finer granularity than that to look smooth.
   */
  Joystick::update();
  Buttons::update(now);
  Battery::update(now);
  Control::update(now);
  UI::update(now);
  Leds::update(now);
}
