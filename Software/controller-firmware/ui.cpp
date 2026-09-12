#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui.h"
#include "config.h"
#include "joystick.h"
#include "buttons.h"
#include "control.h"
#include "battery.h"
#include "link.h"
#include "leds.h"

namespace {

uint32_t s_lastPrintMs = 0;

// Commands arrive as whole lines so that 'v3.45' can carry an argument. The
// Serial Monitor's line ending must therefore be set to Newline.
char    s_line[24];
uint8_t s_len = 0;

void dispatch(const char *line) {
  if (line[0] == '\0') return;

  switch (line[0]) {
    case 'c':
    case 'C':
      Joystick::calibrate();
      break;

    case 't':
    case 'T':
      Serial.println("[LED] self test");
      Leds::selfTest();
      break;

    case 'v':
    case 'V':
      if (line[1] == '\0') {
        Battery::clearSimulated();
        Serial.printf("[BAT] simulation off — real reading %.2f V\n",
                      Battery::volts());
      } else {
        const float v = (float)atof(line + 1);
        Battery::setSimulated(v);
        Serial.printf("[BAT] simulating %.2f V\n", v);
      }
      break;

    default:
      Serial.printf("? unknown command '%s'   (c, t, v<volts>, v)\n", line);
      break;
  }
}

/* Map system state onto the three LEDs.
 *
 * This is the only place that decides what a colour means. Leds animates; it
 * does not interpret. At stage 5 yellow moves to the robot's reported arm
 * state — an edit to this function alone.
 */
void updateIndicators() {
  // Green: stage 4 — reflects whether ESP-NOW sends are actually landing
  // (Link::up(), a 150 ms timeout on the send callback), not just that the
  // radio is configured. Per controller_plan.md stage 4: "Green from
  // send-callback status." Stage 5 will further split this into solid
  // (telemetry flowing) vs blink (ACKs only, firmware wedged) — deliberately
  // not attempted yet, since there is no telemetry to distinguish against.
  Leds::set(Leds::GREEN, Link::up() ? Leds::PULSE : Leds::OFF);

  // Yellow: the local arm latch. Stage 5 re-points this at telemetry.
  Leds::set(Leds::YELLOW, Control::armed() ? Leds::ON : Leds::OFF);

  // Red: this controller's own cell.
  switch (Battery::level()) {
    case Battery::CRITICAL: Leds::set(Leds::RED, Leds::ON);         break;
    case Battery::LOW_WARN: Leds::set(Leds::RED, Leds::BLINK_SLOW); break;
    case Battery::GOOD:
    default:                Leds::set(Leds::RED, Leds::OFF);        break;
  }
}

}  // namespace

namespace UI {

void banner() {
  Serial.println();
  Serial.println("=== Controller — stage 4 (ESP-NOW link) ===");
  Serial.printf("X GPIO%u   Y GPIO%u   ARM GPIO%u   SPEED GPIO%u   "
                "CAPTURE GPIO%u   STICK GPIO%u\n",
                Pins::AXIS_X, Pins::AXIS_Y, Pins::ARM,
                Pins::SPEED, Pins::CAPTURE, Pins::STICK);
  Serial.printf("LED green GPIO%u   yellow GPIO%u   red GPIO%u   "
                "battery sense GPIO%u\n",
                Pins::LED_GREEN, Pins::LED_YELLOW, Pins::LED_RED,
                Pins::BATT_SENSE);
  Serial.println("Module VCC must be on 3.3 V.");
  Serial.println();

  // Report anything held at boot, so it is obvious rather than mysterious.
  for (uint8_t i = 0; i < (uint8_t)Buttons::COUNT; i++) {
    const Buttons::Id id = (Buttons::Id)i;
    if (Buttons::heldAtBoot(id)) {
      Serial.printf("Note: %s is held down at boot — ignored until released.\n",
                    Buttons::name(id));
    }
  }
}

void ready() {
  Serial.println();
  Serial.println("Ready. Starts DISARMED in PRECISION mode.");
  Serial.println("TX columns (and CAP/ZERO flags) are what's sent over ESP-NOW");
  Serial.println("at 50 Hz. LINK shows whether the robot is acking — up/down,");
  Serial.println("with sent/fail counts.");
  Serial.println("Keys (Serial Monitor line ending must be Newline):");
  Serial.println("  c        recalibrate joystick centre");
  Serial.println("  t        re-run the LED self test");
  Serial.println("  v3.45    simulate a cell voltage");
  Serial.println("  v        return to the real reading");
  Serial.println();
}

void handleInput() {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      s_line[s_len] = '\0';
      dispatch(s_line);
      s_len = 0;
      continue;
    }
    if (s_len < (uint8_t)(sizeof(s_line) - 1)) s_line[s_len++] = ch;
  }
}

void update(uint32_t now) {
  if ((now - s_lastPrintMs) < Timing::PRINT_PERIOD_MS) return;
  s_lastPrintMs = now;

  updateIndicators();

  /* While ARM is held, count the hold up. Two seconds is long enough that
   * silent waiting feels like nothing is happening. holdMs() returns 0 once the
   * hold has fired, so a non-zero value always means one is in progress.
   */
  char armField[16];
  const uint32_t held = Buttons::holdMs(Buttons::ARM, now);
  if (held > 0) {
    snprintf(armField, sizeof armField, "%s %.1fs",
             Control::armed() ? "ON" : "off", held / 1000.0f);
  } else {
    snprintf(armField, sizeof armField, "%s", Control::armed() ? "ON" : "off");
  }

  /* Two battery figures, deliberately. The first is the EMA that everything
   * downstream acts on — levels, LEDs, and the arming interlock at 3b. The
   * second is the last unfiltered sample, which is the one the calibration
   * procedure compares against a multimeter: with a ~3 s time constant the
   * filtered value lags the meter, so trimming CAL_FACTOR against it would fold
   * that lag into the constant. Under 'v' simulation the two are equal by
   * construction — setSimulated() bypasses the filter.
   */
  Serial.printf("X %+.2f (%4u)  Y %+.2f (%4u) | ARM %-8s %-9s | "
                "TX %+5d %+5d | SW %s  CAP %-2lu ZERO %-2lu | "
                "BAT %.2fV%s (raw %.2f) %-4s | G:%-5s Y:%-3s R:%-5s | "
                "LINK %-4s TX:%-6lu FAIL:%-4lu\n",
                Joystick::x(), Joystick::rawX(),
                Joystick::y(), Joystick::rawY(),
                armField,
                Control::precision() ? "PRECISION" : "FULL",
                Control::txX(), Control::txY(),
                Buttons::isDown(Buttons::STICK) ? "DOWN" : "up  ",
                (unsigned long)Control::captureCount(),
                (unsigned long)Control::zeroCount(),
                Battery::volts(),
                Battery::simulated() ? "~" : " ",
                Battery::rawVolts(),
                Battery::levelName(),
                Leds::modeName(Leds::GREEN),
                Leds::modeName(Leds::YELLOW),
                Leds::modeName(Leds::RED),
                Link::up() ? "up" : "down",
                (unsigned long)Link::sentCount(),
                (unsigned long)Link::failCount());
}

}  // namespace UI
