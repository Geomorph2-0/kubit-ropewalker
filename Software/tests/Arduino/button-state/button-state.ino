/*
 * button-state.ino — latching pushbutton for ESP32-S3.
 *
 * One press sets the state HIGH and it stays HIGH. The next press sets it LOW
 * and it stays LOW. Repeats indefinitely.
 *
 * WIRING
 *   One switch leg   -> GPIO 14
 *   Its DIAGONAL leg -> GND
 *   No external resistor; the internal pull-up handles it. Pressed reads LOW.
 *
 * The state lives in RAM, so it returns to OFF after a reset or brownout.
 * For an arming latch that is the behaviour you want — do not "fix" it by
 * saving to NVS, or the robot will come back armed after a power glitch.
 */

#include <Arduino.h>

// ---------------------------------------------------------------- config ----

static const uint8_t BUTTON_PIN = 14;

// Set to a free GPIO to drive an LED or a driver enable line that mirrors the
// latch. Leave at -1 for serial output only.
static const int8_t OUTPUT_PIN = -1;

static const uint32_t DEBOUNCE_MS   = 25;
static const uint32_t LONG_PRESS_MS = 800;

static const bool ACTIVE_LOW = true;

// A long press always forces the latch OFF, never ON. A deliberate hold should
// only ever be able to make things safer, not arm something by accident.
static const bool LONG_PRESS_FORCES_OFF = true;

// ----------------------------------------------------------------- state ----

static bool     latched      = false;  // the thing you actually care about
static bool     rawLast      = false;
static bool     stableState  = false;  // debounced button: true == pressed
static uint32_t lastChangeMs = 0;
static uint32_t pressStartMs = 0;
static bool     longFired    = false;
static uint32_t toggleCount  = 0;

// --------------------------------------------------------------- helpers ----

static bool readPressed() {
  const int level = digitalRead(BUTTON_PIN);
  return ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

static void applyLatch() {
  if (OUTPUT_PIN >= 0) {
    digitalWrite((uint8_t)OUTPUT_PIN, latched ? HIGH : LOW);
  }
}

// ----------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  if (OUTPUT_PIN >= 0) {
    pinMode((uint8_t)OUTPUT_PIN, OUTPUT);
  }

  // Seed from the real pin so a button held at boot is not read as a press.
  rawLast      = readPressed();
  stableState  = rawLast;
  lastChangeMs = millis();

  latched = false;
  applyLatch();

  Serial.println();
  Serial.println("=== Latching button ===");
  Serial.printf("Button : GPIO %u (INPUT_PULLUP, active %s)\n",
                BUTTON_PIN, ACTIVE_LOW ? "LOW" : "HIGH");
  if (OUTPUT_PIN >= 0) {
    Serial.printf("Output : GPIO %d mirrors the latch\n", (int)OUTPUT_PIN);
  } else {
    Serial.println("Output : none (serial only)");
  }
  Serial.println("State  : LOW");
  Serial.println();
}

// ------------------------------------------------------------------ loop ----

void loop() {
  const uint32_t now = millis();
  const bool raw = readPressed();

  if (raw != rawLast) {
    rawLast = raw;
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) >= DEBOUNCE_MS && raw != stableState) {
    stableState = raw;

    if (stableState) {
      // Rising edge of a debounced press. This single line is the latch.
      latched = !latched;
      applyLatch();

      toggleCount++;
      pressStartMs = now;
      longFired = false;

      Serial.printf("Toggle #%lu  ->  %s\n",
                    (unsigned long)toggleCount,
                    latched ? "HIGH" : "LOW");
    }
    // Release does nothing. The latch only moves on press.
  }

  if (LONG_PRESS_FORCES_OFF &&
      stableState && !longFired && (now - pressStartMs) >= LONG_PRESS_MS) {
    longFired = true;
    if (latched) {
      latched = false;
      applyLatch();
      Serial.println("Long press  ->  forced LOW");
    }
  }

  // latched is now a plain boolean you can read anywhere:
  //   if (latched) { runMotors(); } else { stopMotors(); }
}
