#pragma once
#include <Arduino.h>

/*
 * config.h — every pin number and tuning constant for the controller.
 *
 * Nothing else in the project hardcodes a GPIO or a threshold. If a value needs
 * changing, it changes here and nowhere else.
 *
 * BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit.
 *        Controller MAC F4:2D:C9:71:7B:0C, robot MAC F4:2D:C9:71:0A:7C.
 */

// ------------------------------------------------------------------- pins ---
/*
 * Both axes are on ADC1. ADC2 belongs to the WiFi driver and its reads fail
 * silently once the radio starts, so from stage 4 onward an ADC2 pin would work
 * perfectly on the bench and die the moment ESP-NOW comes up.
 *
 * Never use GPIO 6-11 (SPI flash) or GPIO 12 (MTDI strapping — an internal
 * pull-up there stops the board booting).
 */
namespace Pins {
  constexpr uint8_t AXIS_X  = 33;   // module VRy — ADC1_CH5 — left / right
  constexpr uint8_t AXIS_Y  = 32;   // module VRx — ADC1_CH4 — up / down

  constexpr uint8_t ARM     = 25;   // header 9
  constexpr uint8_t FREE    = 26;   // header 10 — deliberately unassigned
  constexpr uint8_t SPEED   = 27;   // header 11
  constexpr uint8_t CAPTURE = 14;   // header 12
  constexpr uint8_t STICK   = 13;   // header 15 — joystick push

  /* LEDs. None of these is a strapping pin, input-only, or tied to the flash.
   * They are the VSPI signals, which is irrelevant — nothing here uses hardware
   * SPI. At boot they float as inputs until pinMode(), so the LEDs stay dark
   * through reset instead of flashing.
   */
  constexpr uint8_t LED_GREEN  = 18;   // header 30
  constexpr uint8_t LED_YELLOW = 19;   // header 31
  constexpr uint8_t LED_RED    = 23;   // header 37

  /* Battery sense. Input-only, which is fine for an analog line, and ADC1_CH7
   * so it keeps working once the radio starts at stage 4.
   */
  constexpr uint8_t BATT_SENSE = 35;   // header 6
}

// -------------------------------------------------------------------- adc ---
namespace Adc {
  constexpr uint8_t  BITS        = 12;    // 0..4095
  constexpr uint16_t MAX         = 4095;
  constexpr uint8_t  OVERSAMPLE  = 8;     // averaged per read
  constexpr uint16_t CAL_SAMPLES = 64;    // averaged at boot
}

// ------------------------------------------------------------------ stick ---
namespace Stick {
  /* Sign only — which pot is which axis is settled in Pins above. Whether
   * pushing right reads positive depends on how that pot happens to be wired.
   * Push right and up; flip the matching flag if a sign is backwards. Set by
   * experiment, not by reasoning.
   */
  constexpr bool INVERT_X = false;
  constexpr bool INVERT_Y = false;

  constexpr float DEADZONE = 0.06f;       // fraction of full travel

  // A plausible resting centre. Outside this, something is wrong: stick held
  // during calibration, wrong rail, or a wire off.
  constexpr uint16_t CENTRE_MIN = 1200;
  constexpr uint16_t CENTRE_MAX = 2900;
}

// ----------------------------------------------------------------- timing ---
namespace Timing {
  constexpr uint32_t DEBOUNCE_MS     = 25;
  constexpr uint32_t ARM_HOLD_MS     = 2000;   // hold to arm, and to disarm
  constexpr uint32_t STICK_HOLD_MS   = 2000;   // hold to zero distance
  constexpr uint32_t PRINT_PERIOD_MS = 100;    // status line at 10 Hz

  // A 50%-duty heartbeat reads as a warning. A short flash on a long period
  // reads as "alive", which is what it means.
  constexpr uint32_t PULSE_ON_MS      = 80;
  constexpr uint32_t PULSE_PERIOD_MS  = 1000;
  constexpr uint32_t BLINK_SLOW_MS    = 500;   // half-period
  constexpr uint32_t BLINK_FAST_MS    = 125;   // half-period; reserved for st.5
  constexpr uint32_t SELFTEST_STEP_MS = 200;

  /* Cell voltage does not change fast and EMA_ALPHA below gives a ~3 s time
   * constant, so sampling faster than this buys nothing.
   */
  constexpr uint32_t BATT_SAMPLE_MS   = 250;   // 4 Hz
}

// ------------------------------------------------------------------ drive ---
namespace Drive {
  constexpr float PRECISION_SCALE = 0.40f;     // travel available in precision
}

// ---------------------------------------------------------------- battery ---
/*
 * 1S Li-ion 18650, read through a 2:1 divider on GPIO35.
 *
 * The 2:1 ratio caps the pin at 2.10 V on a full cell. That is deliberate: the
 * ESP32 ADC at 11 dB attenuation is only well-behaved to roughly 2450 mV and
 * clips above it, so halving keeps the whole cell range inside the linear part.
 */
namespace Batt {
  constexpr float   DIVIDER_RATIO = 2.0f;     // (R1 + R2) / R2, equal resistors
  constexpr float   CAL_FACTOR    = 1.000f;   // measure against a multimeter
  constexpr uint8_t OVERSAMPLE    = 16;
  constexpr float   EMA_ALPHA     = 0.08f;    // ~3 s time constant at 4 Hz

  /* Falling thresholds are exact, so a warning appears the moment it is true.
   * Recovery needs HYSTERESIS of margin, or the LED strobes at a boundary.
   */
  constexpr float V_GOOD     = 3.70f;   // above this: healthy
  constexpr float V_LOW      = 3.50f;   // below this: critical
  constexpr float HYSTERESIS = 0.15f;

  /* The arming floor. Below this the latch refuses to close, so a cell that is
   * already too flat to finish a run cannot be the reason the robot stops
   * responding halfway along the rope.
   *
   * It sits below V_LOW on purpose. CRITICAL is a warning about a cell that is
   * nearly done but still perfectly able to drive; this is the hard refusal, and
   * the gap between them is the window in which the red LED is telling you to
   * land before the controller stops letting you take off.
   *
   * No hysteresis, because none is needed: the test runs once, at the instant
   * ARM completes its hold, and never again. Nothing here can oscillate.
   */
  constexpr float V_ARM_MIN  = 3.30f;
}

// ----------------------------------------------------------------- system ---
constexpr uint32_t SERIAL_BAUD = 115200;
