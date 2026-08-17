#pragma once
#include <Arduino.h>

/*
 * leds.h — three status LEDs, driven by mode rather than by digitalWrite.
 *
 * This module knows how to animate a pattern. It has no idea what any pattern
 * means — UI decides that, the same way it decides what goes in the status line.
 * At stage 5, when telemetry arrives, only UI's mapping changes; nothing here
 * moves.
 *
 * Wiring: GPIO -> resistor -> LED anode; cathode -> GND. Size the resistor from
 * the LED's measured forward voltage, especially for green: a modern InGaN green
 * sits at 3.0-3.4 V and will barely light from a 3.3 V pin through 330 ohm.
 */
namespace Leds {

enum Id : uint8_t {
  GREEN = 0,   // heartbeat for now; link state from stage 4
  YELLOW,      // armed
  RED,         // controller cell
  COUNT
};

enum Mode : uint8_t {
  OFF,
  ON,
  PULSE,        // brief flash on a long period — "alive"
  BLINK_SLOW,   // even blink — "attention"
  BLINK_FAST    // reserved for stage 5's degraded-link state
};

// Sets pin modes and runs the power-on self test.
void begin();

/* Walks green, yellow, red, then all three, then dark. Blocking, ~900 ms.
 *
 * It proves every LED, resistor and solder joint on every power-up, and confirms
 * the colour-to-pin mapping. An indicator that silently never lights is worse
 * than no indicator, because a dark LED reads as "all clear". Re-runnable at any
 * time with the 't' serial command.
 */
void selfTest();

// Animation. Call every loop pass — the patterns are computed from `now`.
void update(uint32_t now);

void set(Id id, Mode m);
Mode mode(Id id);

// Short label for the status line: "off", "on", "pulse", "blink", "fast".
const char *modeName(Id id);

}  // namespace Leds
