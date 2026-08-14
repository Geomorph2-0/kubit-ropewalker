#include <Arduino.h>
#include <stdio.h>

#include "ui.h"
#include "config.h"
#include "joystick.h"
#include "buttons.h"
#include "control.h"

namespace {

uint32_t s_lastPrintMs = 0;

}  // namespace

namespace UI {

void banner() {
  Serial.println();
  Serial.println("=== Controller — joystick + buttons (modular) ===");
  Serial.printf("X GPIO%u   Y GPIO%u   ARM GPIO%u   SPEED GPIO%u   "
                "CAPTURE GPIO%u   STICK GPIO%u\n",
                Pins::AXIS_X, Pins::AXIS_Y, Pins::ARM,
                Pins::SPEED, Pins::CAPTURE, Pins::STICK);
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
  Serial.println("TX columns are what will go in the ESP-NOW packet at stage 4.");
  Serial.println("Keys:  c = recalibrate centre");
  Serial.println();
}

void handleInput() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == 'c' || ch == 'C') Joystick::calibrate();
  }
}

void update(uint32_t now) {
  if ((now - s_lastPrintMs) < Timing::PRINT_PERIOD_MS) return;
  s_lastPrintMs = now;

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

  Serial.printf("X %+.2f (%4u)  Y %+.2f (%4u) | ARM %-8s %-9s | "
                "TX %+5d %+5d | SW %s  CAP %-2lu  ZERO %lu\n",
                Joystick::x(), Joystick::rawX(),
                Joystick::y(), Joystick::rawY(),
                armField,
                Control::precision() ? "PRECISION" : "FULL",
                Control::txX(), Control::txY(),
                Buttons::isDown(Buttons::STICK) ? "DOWN" : "up  ",
                (unsigned long)Control::captureCount(),
                (unsigned long)Control::zeroCount());
}

}  // namespace UI
