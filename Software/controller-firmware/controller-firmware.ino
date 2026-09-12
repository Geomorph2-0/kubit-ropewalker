/*
 * controller-firmware.ino — rope-walker ground controller.
 *
 * Stage 4: ESP-NOW control link, controller -> robot, on top of 3b's arm
 * interlock.
 *
 * 3a was purely additive — joystick.*, buttons.* and control.* were untouched.
 * 3b added a single branch to control.cpp: refuse to close the arm latch
 * below Batt::V_ARM_MIN. Stage 4 is additive again — link.* and protocol.h are
 * new files, config.h gains two new Timing constants, and ui.cpp is the one
 * existing file that changes (green now reflects Link::up() instead of a bare
 * heartbeat) because ui.cpp's own header already names itself as the single
 * place LED semantics are allowed to move. joystick.*, buttons.*, battery.*
 * and control.* are untouched by this stage.
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
 *   link.*        stage 4 — builds and sends the ESP-NOW ControlPacket at 50 Hz
 *   protocol.h    wire format shared with the robot — canonical copy lives at
 *                 Software/shared/protocol.h; see that file's header for the
 *                 symlink-vs-copy caveat currently in effect
 *
 * Dependencies flow one way: this file -> control -> {joystick, buttons}, and
 * ui reads everything, now including link. No module calls another sideways,
 * so nothing here has a circular include and each module can be reasoned
 * about alone.
 *
 * Stage 5 adds telemetry back (robot -> controller) without touching link.cpp's
 * send path — only its own new receive path alongside it.
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
 *   Battery sense 35      6        cell+ -> 10k -> node -> 10k -> GND,
 *                                  node -> GPIO35, node -> 0.1uF ceramic -> GND
 *
 * Stage 4 adds no wiring — ESP-NOW is a radio link — but it does retire every
 * ADC2 GPIO from future analog use the instant Link::begin() runs. This
 * board's pin plan already avoids that (see Pins:: comment in config.h and
 * the pin-invalidation note in esp32_devkit_38pin_pinout.md), but it forecloses
 * ever reading GPIO26 — or any other ADC2 pin — with analogRead() from here on.
 *
 * Size the LED resistors from each LED's MEASURED forward voltage. A modern
 * InGaN green sits at 3.0-3.4 V and will barely light from a 3.3 V pin through
 * 330 ohm, while red at 1.9 V is comfortable on the same value.
 *
 * Serial Monitor line ending must be Newline — commands are read as whole lines
 * so that 'v3.45' can carry an argument.
 *
 * ============================================================================
 * TEMPORARY BRING-UP TAP — S3 debug relay on UART0 (GPIO1 TX0 / GPIO3 RX0)
 * ============================================================================
 * For bring-up only, GPIO1 and GPIO3 — this board's own USB-serial pins,
 * shared with the onboard CP2102 — are ALSO wired out to the ESP32-S3
 * (Software/s3-uart-bridge/), so every line this firmware prints reaches the
 * S3's own USB port with no code changes: UART0 already carries it verbatim.
 *
 * *** NEVER plug this board's own USB cable in while that wire is connected.
 * *** GPIO3 (RX0) is driven by the CP2102 whenever USB is attached. With the
 * *** S3's TX also wired onto GPIO3, the CP2102 and the S3 become two active
 * *** drivers on one node — a direct drive-against-drive conflict whenever
 * *** they disagree, which can degrade or damage one of the two chips. This
 * *** is the same class of habit as never having USB and the MT3608 both
 * *** connected (see the power-chain doc) — pick ONE: this board's own USB,
 * *** or the S3 wire. Never both at once.
 *
 * This is scaffolding, not a designed link: it disappears once bring-up is
 * done, replaced by either a second USB cable on the bench or the real
 * ESP-NOW + robot-UART path in the field.
 */

#include <Arduino.h>

#include "config.h"
#include "joystick.h"
#include "buttons.h"
#include "battery.h"
#include "control.h"
#include "link.h"
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
  Link::begin();        // brings up WiFi STA + ESP-NOW; prints its own status
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
   * Link runs after Control for the same reason: it sends Control's output,
   * so it must see this pass's decision, not the previous one. It is called
   * every pass, unthrottled, like everything else here — its own internal
   * timers decide when to actually transmit (see link.h for why the
   * event-latching half must run every pass even though sending doesn't).
   *
   * Leds runs last and every pass: UI sets modes on its 100 ms tick, but the
   * animation needs finer granularity than that to look smooth.
   */
  Joystick::update();
  Buttons::update(now);
  Battery::update(now);
  Control::update(now);
  Link::update(now);
  UI::update(now);
  Leds::update(now);
}
