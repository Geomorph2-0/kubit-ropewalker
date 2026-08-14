/*
 * controller-firmware.ino — rope-walker ground controller.
 *
 * Modular rebuild of stage 2. Behaviour is identical to
 * controller-stage2-buttons.ino; only the organisation has changed. If the
 * serial output differs from the single-file version in any way, that is a bug
 * in the refactor, not an intended improvement.
 *
 * TARGET BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit.
 *               Controller MAC F4:2D:C9:71:7B:0C.
 *
 * FILES
 *   config.h      every pin and tuning constant — the only place GPIOs appear
 *   joystick.*    calibration, oversampling, deadzone, normalisation
 *   buttons.*     debounce, press/release/long-hold edges
 *   control.*     arm latch, speed mode, transmitted intent — the policy layer
 *   ui.*          status line and serial commands
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
 */

#include <Arduino.h>

#include "config.h"
#include "joystick.h"
#include "buttons.h"
#include "control.h"
#include "ui.h"

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  Buttons::begin();    // first: banner reports anything held at boot
  UI::banner();
  Joystick::begin();   // configures the ADC, then calibrates and prints
  Control::begin();
  UI::ready();
}

void loop() {
  const uint32_t now = millis();

  UI::handleInput();

  // Inputs, then policy, then output. That order is the architecture — Control
  // must see this pass's stick position before deciding whether arming is
  // allowed, and UI must run last so it displays the decision just made.
  Joystick::update();
  Buttons::update(now);
  Control::update(now);
  UI::update(now);
}
