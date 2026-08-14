#include <Arduino.h>
#include <math.h>

#include "control.h"
#include "buttons.h"
#include "joystick.h"
#include "config.h"

namespace {

bool     s_armed     = false;
bool     s_precision = true;    // start in the safer mode
uint32_t s_captures  = 0;
uint32_t s_zeros     = 0;

bool s_captureEvent = false;
bool s_zeroEvent    = false;

int16_t s_txX = 0;
int16_t s_txY = 0;

}  // namespace

namespace Control {

void begin() {
  s_armed     = false;
  s_precision = true;
  s_captures  = 0;
  s_zeros     = 0;
}

void update(uint32_t now) {
  (void)now;

  s_captureEvent = false;
  s_zeroEvent    = false;

  // ---- ARM ---------------------------------------------------------------
  if (Buttons::pressed(Buttons::ARM)) {
    Serial.printf("[ARM] hold %.0f s to %s...\n",
                  Timing::ARM_HOLD_MS / 1000.0f,
                  s_armed ? "DISARM" : "ARM");
  }

  // A release with no long-hold behind it means they let go too soon. Say so,
  // rather than leaving a short tap looking like a dead button.
  if (Buttons::released(Buttons::ARM) && !Buttons::heldLong(Buttons::ARM)) {
    Serial.println("[ARM] released early — no change");
  }

  if (Buttons::heldLong(Buttons::ARM)) {
    if (s_armed) {
      s_armed = false;
      Serial.println("[ARM] *** DISARMED ***");
    } else if (!Joystick::centred()) {
      // Arming onto a deflected stick would command speed the instant the latch
      // closes. Release and hold again once it is centred.
      Serial.printf("[ARM] refused — centre the stick first (X %+.2f  Y %+.2f)\n",
                    Joystick::x(), Joystick::y());
    } else {
      s_armed = true;
      Serial.println("[ARM] *** ARMED ***");
    }
  }

  // ---- SPEED -------------------------------------------------------------
  if (Buttons::pressed(Buttons::SPEED)) {
    s_precision = !s_precision;
    Serial.printf("[SPEED] %s\n", s_precision ? "PRECISION" : "FULL");
  }

  // ---- CAPTURE -----------------------------------------------------------
  if (Buttons::pressed(Buttons::CAPTURE)) {
    s_captures++;
    s_captureEvent = true;
    Serial.printf("[CAPTURE] #%lu\n", (unsigned long)s_captures);
  }

  // ---- ZERO DISTANCE -----------------------------------------------------
  // Long hold only. A stick click is the easiest thing to hit by accident while
  // driving, and an accidental reset silently corrupts every distance reading
  // for the rest of the run.
  if (Buttons::heldLong(Buttons::STICK)) {
    s_zeros++;
    s_zeroEvent = true;
    Serial.printf("[STICK] zero distance #%lu\n", (unsigned long)s_zeros);
  }

  // ---- transmitted intent ------------------------------------------------
  const float scale = s_armed ? (s_precision ? Drive::PRECISION_SCALE : 1.0f)
                              : 0.0f;
  s_txX = (int16_t)lroundf(Joystick::x() * scale * 1000.0f);
  s_txY = (int16_t)lroundf(Joystick::y() * scale * 1000.0f);
}

bool     armed()        { return s_armed; }
bool     precision()    { return s_precision; }
int16_t  txX()          { return s_txX; }
int16_t  txY()          { return s_txY; }
uint32_t captureCount() { return s_captures; }
uint32_t zeroCount()    { return s_zeros; }
bool     captureEvent() { return s_captureEvent; }
bool     zeroEvent()    { return s_zeroEvent; }

}  // namespace Control
