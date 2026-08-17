#pragma once
#include <Arduino.h>

/*
 * battery.h — the controller's own 1S Li-ion cell, measured through a 2:1
 * divider on GPIO35.
 *
 * Reports facts only. It says the cell is at 3.42 V and that this counts as
 * CRITICAL; it does not decide what to do about that. The refuse-to-arm rule
 * lands in Control at stage 3b, next to the stick-centre interlock, so the whole
 * arming policy stays readable in one place.
 *
 * WIRING
 *   CELL + --[ R1 ]--+--[ R2 ]-- GND        equal values, ratio 2:1
 *                    |
 *                GPIO35 --+-- C -- GND      100 nF or more
 *   CELL - ------------------------ GND     shared with the board
 *
 * Sense the CELL, never a regulated rail. Once the MT3608 is fitted its output
 * is 5.00 V whether the cell is full or nearly flat, so a reading taken there
 * would be a constant and tell you nothing.
 */
namespace Battery {

enum Level : uint8_t {
  GOOD,        // >= V_GOOD
  LOW_WARN,    // between V_LOW and V_GOOD
  CRITICAL     // < V_LOW
};

/* Configures the pin and seeds the filter with one real reading.
 *
 * Seeding matters: without it the 3-second average ramps up from zero and the
 * controller reports a false CRITICAL for the first few seconds of every
 * power-up, which would train you to ignore a red LED.
 */
void begin();

// Samples on its own Timing::BATT_SAMPLE_MS tick. Safe to call every pass.
void update(uint32_t now);

float volts();      // filtered — what the LED and any policy should use
float rawVolts();   // unfiltered last sample — for calibration only

Level level();              // hysteresis applied
const char *levelName();    // "GOOD", "LOW", "CRIT"

/* Inject a voltage for testing. The whole level, hysteresis and LED chain then
 * behaves as though the cell were really there, and the filter is bypassed so
 * the value takes effect immediately rather than easing in over three seconds.
 *
 * Without this, exercising three thresholds and their hysteresis means actually
 * flattening a cell — hours per test, and hard on the battery.
 */
void setSimulated(float v);
void clearSimulated();
bool simulated();

}  // namespace Battery
