#pragma once
#include <Arduino.h>

/*
 * joystick.h — one analog stick, read and normalised.
 *
 * Reports facts only. It has no idea what a speed mode or an arm latch is.
 */
namespace Joystick {

  // Configures the ADC and takes an initial centre reading. Prints the measured
  // centre and warns if it looks implausible.
  void begin();

  // Re-measure the resting centre. Hands off the stick. Also driven by the 'c'
  // serial command.
  void calibrate();

  // Sample both axes. Call once per loop pass.
  void update();

  float x();          // -1.00 .. +1.00, deadzone applied, sign corrected
  float y();

  uint16_t rawX();    // raw averaged counts, for diagnostics
  uint16_t rawY();

  uint16_t centreX(); // measured centre, for diagnostics
  uint16_t centreY();

  // True when both axes sit inside the deadzone. The arming interlock uses this.
  bool centred();

}  // namespace Joystick
