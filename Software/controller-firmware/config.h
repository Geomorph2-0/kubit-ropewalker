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
 * silently once the radio starts, so from stage 4 onward an ADC2 pin would
 * work perfectly on the bench and die the moment ESP-NOW comes up.
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

  // Stage 3 will add: LED_GREEN 18, LED_YELLOW 19, LED_RED 23, BATT_SENSE 35.
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
}

// ------------------------------------------------------------------ drive ---
namespace Drive {
  constexpr float PRECISION_SCALE = 0.40f;     // travel available in precision
}

// ----------------------------------------------------------------- system ---
constexpr uint32_t SERIAL_BAUD = 115200;
