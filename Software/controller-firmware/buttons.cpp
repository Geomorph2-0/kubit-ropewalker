#include <Arduino.h>

#include "buttons.h"
#include "config.h"

namespace {

struct Btn {
  // config
  uint8_t     pin;
  const char *label;
  uint32_t    longMs;        // 0 = no long-hold behaviour

  // state
  bool        rawLast;
  bool        stable;        // debounced; true == pressed
  uint32_t    lastChangeMs;
  uint32_t    pressStartMs;
  bool        longFired;
  bool        bootHeld;

  // one-shot edges, valid only for the pass that set them
  bool        pressedEdge;
  bool        releasedEdge;
  bool        longEdge;
};

Btn s_btn[Buttons::COUNT] = {
  { Pins::ARM,     "ARM",     Timing::ARM_HOLD_MS   },
  { Pins::SPEED,   "SPEED",   0                     },
  { Pins::CAPTURE, "CAPTURE", 0                     },
  { Pins::STICK,   "STICK",   Timing::STICK_HOLD_MS },
};

inline bool valid(Buttons::Id id) { return id < Buttons::COUNT; }

}  // namespace

namespace Buttons {

void begin() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < COUNT; i++) {
    Btn &b = s_btn[i];
    pinMode(b.pin, INPUT_PULLUP);

    /* Seed from the real pin so a button held through boot is not seen as a
     * fresh press, and pre-fire its long-hold so holding it through a reset
     * does nothing either.
     */
    b.rawLast      = (digitalRead(b.pin) == LOW);
    b.stable       = b.rawLast;
    b.bootHeld     = b.stable;
    b.lastChangeMs = now;
    b.pressStartMs = now;
    b.longFired    = b.stable;

    b.pressedEdge = b.releasedEdge = b.longEdge = false;
  }
}

void update(uint32_t now) {
  for (uint8_t i = 0; i < COUNT; i++) {
    Btn &b = s_btn[i];

    b.pressedEdge  = false;
    b.releasedEdge = false;
    b.longEdge     = false;

    const bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.rawLast) {
      b.rawLast      = raw;
      b.lastChangeMs = now;
    }

    if ((now - b.lastChangeMs) >= Timing::DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      if (b.stable) {
        b.pressedEdge  = true;
        b.pressStartMs = now;
        b.longFired    = false;
      } else {
        b.releasedEdge = true;
      }
    }

    if (b.longMs > 0 && b.stable && !b.longFired &&
        (now - b.pressStartMs) >= b.longMs) {
      b.longFired = true;
      b.longEdge  = true;
    }
  }
}

bool pressed(Id id)    { return valid(id) && s_btn[id].pressedEdge; }
bool released(Id id)   { return valid(id) && s_btn[id].releasedEdge; }
bool heldLong(Id id)   { return valid(id) && s_btn[id].longEdge; }
bool isDown(Id id)     { return valid(id) && s_btn[id].stable; }
bool heldAtBoot(Id id) { return valid(id) && s_btn[id].bootHeld; }

uint32_t holdMs(Id id, uint32_t now) {
  if (!valid(id)) return 0;
  const Btn &b = s_btn[id];
  if (!b.stable || b.longFired) return 0;
  return now - b.pressStartMs;
}

const char *name(Id id) { return valid(id) ? s_btn[id].label : "?"; }

}  // namespace Buttons
