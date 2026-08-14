#pragma once
#include <Arduino.h>

/*
 * buttons.h — debounced inputs with press, release and long-hold edges.
 *
 * Reports facts only. It knows that ARM was held for two seconds; it has no idea
 * what arming means. That decision belongs to Control.
 *
 * Every button is wired GPIO -> switch -> GND and uses the internal pull-up, so
 * pressed reads LOW.
 */
namespace Buttons {

enum Id : uint8_t {
  ARM = 0,
  SPEED,
  CAPTURE,
  STICK,
  COUNT
};

// Sets pin modes and seeds each debouncer from the real pin state, so a button
// held through boot is not read as a fresh press and cannot fire its long-hold.
void begin();

// Recompute levels and edges. Call once per loop pass, before Control.
void update(uint32_t now);

/* Edge queries. These are one-shot: true only during the loop pass in which the
 * edge occurred. That is what makes "fires once per press" fall out naturally
 * instead of needing extra latches in the caller.
 */
bool pressed(Id id);    // debounced press edge
bool released(Id id);   // debounced release edge
bool heldLong(Id id);   // fires once, at the button's configured hold time

// Level query: is it down right now (debounced).
bool isDown(Id id);

/* Milliseconds held so far, for showing a countdown while the user waits.
 * Returns 0 when the button is up, and also once its long-hold has already
 * fired — so a non-zero value always means "a hold is in progress".
 */
uint32_t holdMs(Id id, uint32_t now);

// True if this button was already down when begin() ran.
bool heldAtBoot(Id id);

const char *name(Id id);

}  // namespace Buttons
