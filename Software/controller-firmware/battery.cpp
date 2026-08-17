#include <Arduino.h>

#include "battery.h"
#include "config.h"

namespace {

float           s_filtered = 0.0f;
float           s_raw      = 0.0f;
Battery::Level  s_level    = Battery::GOOD;
uint32_t        s_lastSampleMs = 0;

bool  s_sim      = false;
float s_simVolts = 0.0f;

/* One measurement of the cell, in volts.
 *
 * analogReadMilliVolts() rather than analogRead(): it applies the per-chip ADC
 * calibration burned into eFuse at the factory, which corrects most of the
 * ESP32's non-linearity. Raw counts scaled by 3.3/4095 can be nearly 10% out.
 */
float measure() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < Batt::OVERSAMPLE; i++) {
    sum += (uint32_t)analogReadMilliVolts(Pins::BATT_SENSE);
  }
  const float pinVolts = (float)sum / (float)Batt::OVERSAMPLE / 1000.0f;
  return pinVolts * Batt::DIVIDER_RATIO * Batt::CAL_FACTOR;
}

/* Sequential ifs rather than a switch, so a single call can cascade more than
 * one level when the voltage jumps — which it does constantly while testing with
 * the 'v' command. The rising and falling conditions can never both be true, so
 * this cannot oscillate.
 */
void updateLevel(float v) {
  if (s_level == Battery::GOOD     && v <  Batt::V_GOOD)                    s_level = Battery::LOW_WARN;
  if (s_level == Battery::LOW_WARN && v <  Batt::V_LOW)                     s_level = Battery::CRITICAL;
  if (s_level == Battery::CRITICAL && v > (Batt::V_LOW  + Batt::HYSTERESIS)) s_level = Battery::LOW_WARN;
  if (s_level == Battery::LOW_WARN && v > (Batt::V_GOOD + Batt::HYSTERESIS)) s_level = Battery::GOOD;
}

}  // namespace

namespace Battery {

void begin() {
  /* Set attenuation on this pin explicitly rather than relying on the global
   * call in Joystick::begin(), so the two modules can be initialised in any
   * order without a silent dependency between them.
   */
  analogSetPinAttenuation(Pins::BATT_SENSE, ADC_11db);

  s_raw      = measure();
  s_filtered = s_raw;          // seed, do not ramp from zero
  s_level    = GOOD;
  updateLevel(s_filtered);

  s_lastSampleMs = millis();

  Serial.printf("Battery: %.2f V at boot (%s)\n", s_filtered, levelName());
}

void update(uint32_t now) {
  if (s_sim) {
    // Bypass the filter entirely: a simulated value should take effect at once,
    // not ease in over three seconds.
    s_raw      = s_simVolts;
    s_filtered = s_simVolts;
    updateLevel(s_filtered);
    return;
  }

  if ((now - s_lastSampleMs) < Timing::BATT_SAMPLE_MS) return;
  s_lastSampleMs = now;

  s_raw = measure();

  // Exponential moving average. Motor and radio current sag the cell for tens of
  // milliseconds at a time; that is a real voltage change, not noise, so it is
  // filtered here rather than in hardware.
  s_filtered += Batt::EMA_ALPHA * (s_raw - s_filtered);

  updateLevel(s_filtered);
}

float volts()    { return s_filtered; }
float rawVolts() { return s_raw; }
Level level()    { return s_level; }

const char *levelName() {
  switch (s_level) {
    case LOW_WARN: return "LOW";
    case CRITICAL: return "CRIT";
    case GOOD:
    default:       return "GOOD";
  }
}

void setSimulated(float v) {
  s_sim      = true;
  s_simVolts = v;
}

void clearSimulated() {
  s_sim = false;
  // Re-seed from reality so the filter does not carry the simulated value.
  s_raw      = measure();
  s_filtered = s_raw;
  updateLevel(s_filtered);
}

bool simulated() { return s_sim; }

}  // namespace Battery
