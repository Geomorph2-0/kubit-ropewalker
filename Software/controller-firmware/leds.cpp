#include <Arduino.h>

#include "leds.h"
#include "config.h"

namespace {

const uint8_t kPin[Leds::COUNT] = {
  Pins::LED_GREEN,
  Pins::LED_YELLOW,
  Pins::LED_RED,
};

Leds::Mode s_mode[Leds::COUNT] = { Leds::OFF, Leds::OFF, Leds::OFF };

/* Patterns are derived from absolute millis() rather than from a per-LED phase
 * counter. Two consequences, both wanted: blinking LEDs stay in step with each
 * other, and a mode change takes effect on the very next pass with no state to
 * reset.
 */
bool patternOn(Leds::Mode m, uint32_t now) {
  switch (m) {
    case Leds::ON:
      return true;
    case Leds::PULSE:
      return (now % Timing::PULSE_PERIOD_MS) < Timing::PULSE_ON_MS;
    case Leds::BLINK_SLOW:
      return ((now / Timing::BLINK_SLOW_MS) & 1U) == 0U;
    case Leds::BLINK_FAST:
      return ((now / Timing::BLINK_FAST_MS) & 1U) == 0U;
    case Leds::OFF:
    default:
      return false;
  }
}

inline bool valid(Leds::Id id) { return id < Leds::COUNT; }

}  // namespace

namespace Leds {

void selfTest() {
  // One at a time, in declaration order, so a mismatch between the colour you
  // expect and the pin it is on is immediately obvious.
  for (uint8_t i = 0; i < COUNT; i++) {
    digitalWrite(kPin[i], HIGH);
    delay(Timing::SELFTEST_STEP_MS);
    digitalWrite(kPin[i], LOW);
  }

  // Then all three together — catches a shared fault, like a common cathode
  // rail that is only connected for some of them.
  for (uint8_t i = 0; i < COUNT; i++) digitalWrite(kPin[i], HIGH);
  delay(Timing::SELFTEST_STEP_MS + 100);
  for (uint8_t i = 0; i < COUNT; i++) digitalWrite(kPin[i], LOW);
}

void begin() {
  for (uint8_t i = 0; i < COUNT; i++) {
    pinMode(kPin[i], OUTPUT);
    digitalWrite(kPin[i], LOW);
    s_mode[i] = OFF;
  }
  selfTest();
}

void update(uint32_t now) {
  for (uint8_t i = 0; i < COUNT; i++) {
    digitalWrite(kPin[i], patternOn(s_mode[i], now) ? HIGH : LOW);
  }
}

void set(Id id, Mode m) {
  if (valid(id)) s_mode[id] = m;
}

Mode mode(Id id) { return valid(id) ? s_mode[id] : OFF; }

const char *modeName(Id id) {
  switch (mode(id)) {
    case ON:         return "on";
    case PULSE:      return "pulse";
    case BLINK_SLOW: return "blink";
    case BLINK_FAST: return "fast";
    case OFF:
    default:         return "off";
  }
}

}  // namespace Leds
