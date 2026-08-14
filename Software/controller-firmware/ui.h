#pragma once
#include <Arduino.h>

/*
 * ui.h — everything the human sees or types.
 *
 * Owns the periodic status line and serial command parsing. Modules print their
 * own setup-time diagnostics (Joystick reports its measured centre) and Control
 * prints its own event lines, because those are inseparable from the branch that
 * decides them. But the dense, once-per-100 ms status line has exactly one owner
 * — it grows LED state at stage 3 and link state at stage 5, and that only stays
 * manageable if it lives in one place.
 *
 * UI reads from every other module. Nothing reads from UI.
 */
namespace UI {

// Header, pin summary, and a note for anything held down at boot. Call after
// Buttons::begin() so the held-at-boot report has something to report.
void banner();

// The "ready" block. Call after Joystick::begin(), so it lands below the
// calibration output.
void ready();

// Serial commands. Currently: 'c' recalibrates the joystick centre.
void handleInput();

// Status line, throttled to Timing::PRINT_PERIOD_MS.
void update(uint32_t now);

}  // namespace UI
