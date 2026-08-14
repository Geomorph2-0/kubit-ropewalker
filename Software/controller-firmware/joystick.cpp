#include <Arduino.h>
#include <math.h>

#include "joystick.h"
#include "config.h"

namespace {

uint16_t s_xCentre = 2048;
uint16_t s_yCentre = 2048;
uint16_t s_rawX = 0;
uint16_t s_rawY = 0;
float    s_x = 0.0f;
float    s_y = 0.0f;

// Cheap pots and the ESP32 ADC together give a few tens of counts of jitter.
// Averaging costs microseconds and removes most of it.
uint16_t readAveraged(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < Adc::OVERSAMPLE; i++) {
    sum += (uint32_t)analogRead(pin);
  }
  return (uint16_t)(sum / Adc::OVERSAMPLE);
}

/* Map a raw reading to -1.0 .. +1.0 around a measured centre.
 *
 * Centre is almost never 2048 — mechanical tolerance and ADC nonlinearity put it
 * anywhere from roughly 1700 to 2300. Scaling each side against its own span
 * means full deflection still reaches 1.0 in both directions on an off-centre
 * stick, instead of hitting 0.85 one way and clipping the other.
 */
float normalise(uint16_t raw, uint16_t centre) {
  float v;
  if (raw >= centre) {
    const uint16_t span = (Adc::MAX > centre) ? (uint16_t)(Adc::MAX - centre) : 1;
    v = (float)(raw - centre) / (float)span;
  } else {
    const uint16_t span = (centre > 0) ? centre : 1;
    v = -((float)(centre - raw) / (float)span);
  }

  if (v >  1.0f) v =  1.0f;
  if (v < -1.0f) v = -1.0f;

  // Rescale outside the deadzone so there is no discontinuity at its edge.
  // Without this the stick visibly jumps from 0 to 0.06 as it leaves centre.
  if (fabsf(v) < Stick::DEADZONE) return 0.0f;
  const float sign = (v > 0.0f) ? 1.0f : -1.0f;
  return sign * (fabsf(v) - Stick::DEADZONE) / (1.0f - Stick::DEADZONE);
}

}  // namespace

namespace Joystick {

void calibrate() {
  Serial.println("Calibrating — hands off the stick...");
  delay(500);

  uint32_t xSum = 0, ySum = 0;
  for (uint16_t s = 0; s < Adc::CAL_SAMPLES; s++) {
    xSum += (uint32_t)analogRead(Pins::AXIS_X);
    ySum += (uint32_t)analogRead(Pins::AXIS_Y);
    delay(2);
  }
  s_xCentre = (uint16_t)(xSum / Adc::CAL_SAMPLES);
  s_yCentre = (uint16_t)(ySum / Adc::CAL_SAMPLES);

  Serial.printf("  centre  X=%u  Y=%u\n", s_xCentre, s_yCentre);

  if (s_xCentre < Stick::CENTRE_MIN || s_xCentre > Stick::CENTRE_MAX ||
      s_yCentre < Stick::CENTRE_MIN || s_yCentre > Stick::CENTRE_MAX) {
    Serial.println("  *** CENTRE OUT OF RANGE — expected roughly 1700-2300 ***");
    Serial.println("  Stick touched during calibration? VCC on 3.3 V? Wiring?");
  }
}

void begin() {
  analogReadResolution(Adc::BITS);

  /* Full-scale attenuation, so the usable input range covers roughly 0-3.3 V.
   * Without it the ADC saturates near 1.1 V and the stick appears to hit its
   * limit a third of the way through its travel.
   */
  analogSetAttenuation(ADC_11db);

  calibrate();
}

void update() {
  s_rawX = readAveraged(Pins::AXIS_X);
  s_rawY = readAveraged(Pins::AXIS_Y);

  s_x = normalise(s_rawX, s_xCentre);
  s_y = normalise(s_rawY, s_yCentre);

  if (Stick::INVERT_X) s_x = -s_x;
  if (Stick::INVERT_Y) s_y = -s_y;
}

float    x()       { return s_x; }
float    y()       { return s_y; }
uint16_t rawX()    { return s_rawX; }
uint16_t rawY()    { return s_rawY; }
uint16_t centreX() { return s_xCentre; }
uint16_t centreY() { return s_yCentre; }

bool centred() { return (s_x == 0.0f && s_y == 0.0f); }

}  // namespace Joystick
