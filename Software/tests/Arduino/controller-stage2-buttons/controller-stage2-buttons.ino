/*
 * controller-stage2-buttons.ino — Stage 2 of the controller build.
 *
 * TARGET BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit (the controller,
 * MAC F4:2D:C9:71:7B:0C).
 *
 * SCOPE: stage 1's joystick, plus all four buttons and the stick click, with the
 * arming logic that everything downstream depends on. Still no LEDs (stage 3)
 * and no radio (stage 4). Serial output only.
 *
 * WIRING — additions to stage 1
 *
 *   Signal        GPIO    Header   Wiring
 *   ------        ----    ------   ------
 *   Joystick X    33      8        module VRy  (see note below)
 *   Joystick Y    32      7        module VRx
 *   Stick SW      13      15       switch -> GND
 *   ARM           25      9        switch -> GND
 *   (free)        26      10       nothing fitted
 *   SPEED         27      11       switch -> GND
 *   CAPTURE       14      12       switch -> GND
 *
 * Every button wires GPIO -> switch -> GND and uses the internal pull-up, so
 * pressed reads LOW. No external resistors.
 *
 * The axes are crossed in software: on this module the pot marked VRy moves with
 * left-right motion. The wiring above is correct as-is — do not swap wires.
 *
 * GPIO 26 is deliberately unassigned. GPIO 12 is never used: it is the MTDI
 * strapping pin, and an internal pull-up on it stops the board booting.
 *
 * WHAT EACH CONTROL DOES
 *
 *   ARM      latching, and deliberate. Motors ignore the stick until this is on.
 *            A 2 s hold arms; another 2 s hold disarms. A short press does
 *            nothing at all, so neither state can be changed by a brush against
 *            the handset. The status line counts the hold up so you can see it
 *            working before there is an LED to show it.
 *
 *            Note that disarming being slow costs nothing: the fast stop is
 *            letting go of the stick, which spring-returns to zero throttle in
 *            under 100 ms. ARM is the "make it safe" control, not the panic one.
 *   SPEED    toggles PRECISION (40% of travel) and FULL. Precision for the
 *            launch and anything that looks like a knot; full for clear rope.
 *   CAPTURE  momentary. One event per press. Becomes the SAR image trigger.
 *   STICK    hold 2 s to zero the distance count. A short click does nothing,
 *            deliberately: a stick click is the easiest thing to hit by accident
 *            while driving, and an accidental reset silently corrupts every
 *            distance reading for the rest of the run.
 *
 * TWO SAFETY RULES BUILT IN HERE
 *
 *   1. Arming is refused unless the stick is centred. Arming with the stick
 *      deflected would command speed the instant the latch closes.
 *   2. The transmitted axis values are forced to zero while disarmed. The robot
 *      will also ignore them, but sending zeros means no single bug on either
 *      side can drive the motors on its own.
 *
 * Arm state lives in RAM only. After a reset or brownout it comes back OFF. Do
 * not "fix" this by saving it to NVS — a robot that comes back armed after a
 * power glitch is exactly what you do not want on a rope.
 *
 * BOARD SETTINGS
 *   Board            : ESP32 Dev Module
 *   Flash Size       : 4MB (32Mb)
 *   Partition Scheme : Default 4MB with spiffs
 *   Port             : /dev/ttyUSB0  (CP2102 bridge)
 *
 * Close the Serial Monitor before uploading.
 *
 * EXIT TEST FOR THIS STAGE
 *   1. Each of the five controls prints its own name when pressed.
 *   2. A short press of ARM changes nothing and says "released early".
 *   3. A 2 s hold arms; a further 2 s hold disarms. The hold timer is visible
 *      in the status line while the button is down.
 *   4. ARM is refused while the stick is off centre, and says so.
 *   5. Holding any button through a reset triggers nothing.
 *   5. SPEED visibly rescales the TX numbers (full vs 40%).
 *   6. CAPTURE fires exactly once per press, not repeatedly while held.
 *   7. Stick held 2 s fires zero-distance once; a short click does nothing.
 *   8. TX reads +0 +0 whenever ARM is OFF, whatever the stick is doing.
 *
 * SERIAL COMMANDS
 *   c   recalibrate joystick centre — hands off the stick first
 */

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------- config ----

static const uint8_t PIN_AXIS_X = 33;   // module VRy — ADC1_CH5 — left / right
static const uint8_t PIN_AXIS_Y = 32;   // module VRx — ADC1_CH4 — up / down

static const bool INVERT_X = false;     // set by experiment, not by reasoning
static const bool INVERT_Y = false;

static const uint8_t  ADC_BITS        = 12;
static const uint16_t ADC_MAX         = 4095;
static const uint8_t  OVERSAMPLE      = 8;
static const uint16_t CAL_SAMPLES     = 64;
static const float    DEADZONE        = 0.06f;
static const uint32_t PRINT_PERIOD_MS = 100;   // 10 Hz

static const uint16_t CENTRE_MIN = 1200;       // plausible resting centre
static const uint16_t CENTRE_MAX = 2900;

static const uint32_t DEBOUNCE_MS    = 25;
static const uint32_t ARM_HOLD_MS    = 2000;   // hold to arm, and to disarm
static const uint32_t STICK_HOLD_MS  = 2000;   // hold to zero distance

static const float PRECISION_SCALE = 0.40f;    // travel available in precision mode

// ---------------------------------------------------------------- buttons ---

/* One debouncer, four instances. The edge flags are one-shot: they are set by
 * buttonUpdate() and are only valid for that pass of loop(), which is what makes
 * "fires once per press" fall out naturally instead of needing extra latches.
 */
struct Button {
  // config
  uint8_t     pin;
  const char *name;
  uint32_t    longMs;        // 0 = no long-press behaviour

  // state
  bool        rawLast;
  bool        stable;        // debounced; true == pressed
  uint32_t    lastChangeMs;
  uint32_t    pressStartMs;
  bool        longFired;

  // one-shot edges
  bool        pressedEdge;
  bool        releasedEdge;
  bool        longEdge;
};

enum { BTN_ARM = 0, BTN_SPEED, BTN_CAPTURE, BTN_STICK, BTN_COUNT };

static Button buttons[BTN_COUNT] = {
  { 25, "ARM",     ARM_HOLD_MS   },
  { 27, "SPEED",   0             },
  { 14, "CAPTURE", 0             },
  { 13, "STICK",   STICK_HOLD_MS },
};

// ----------------------------------------------------------------- state ----

static uint16_t xCentre = 2048, yCentre = 2048;
static uint32_t lastPrintMs = 0;

static bool     armed        = false;
static bool     precision    = true;    // start in the safer mode
static uint32_t captureCount = 0;
static uint32_t distanceZeroCount = 0;

static float axisX = 0.0f, axisY = 0.0f;
static uint16_t rawX = 0, rawY = 0;

// --------------------------------------------------------- joystick helpers --

static uint16_t readAveraged(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) {
    sum += (uint32_t)analogRead(pin);
  }
  return (uint16_t)(sum / OVERSAMPLE);
}

/* Map a raw reading to -1.0 .. +1.0 around a measured centre. Each side is
 * scaled against its own span, so full deflection reaches 1.0 in both directions
 * even on an off-centre stick. The deadzone is rescaled out so there is no jump
 * as the stick leaves centre.
 */
static float normalise(uint16_t raw, uint16_t centre) {
  float v;
  if (raw >= centre) {
    const uint16_t span = (ADC_MAX > centre) ? (uint16_t)(ADC_MAX - centre) : 1;
    v = (float)(raw - centre) / (float)span;
  } else {
    const uint16_t span = (centre > 0) ? centre : 1;
    v = -((float)(centre - raw) / (float)span);
  }

  if (v >  1.0f) v =  1.0f;
  if (v < -1.0f) v = -1.0f;

  if (fabsf(v) < DEADZONE) return 0.0f;
  const float sign = (v > 0.0f) ? 1.0f : -1.0f;
  return sign * (fabsf(v) - DEADZONE) / (1.0f - DEADZONE);
}

static void calibrate() {
  Serial.println("Calibrating — hands off the stick...");
  delay(500);

  uint32_t xSum = 0, ySum = 0;
  for (uint16_t s = 0; s < CAL_SAMPLES; s++) {
    xSum += (uint32_t)analogRead(PIN_AXIS_X);
    ySum += (uint32_t)analogRead(PIN_AXIS_Y);
    delay(2);
  }
  xCentre = (uint16_t)(xSum / CAL_SAMPLES);
  yCentre = (uint16_t)(ySum / CAL_SAMPLES);

  Serial.printf("  centre  X=%u  Y=%u\n", xCentre, yCentre);

  if (xCentre < CENTRE_MIN || xCentre > CENTRE_MAX ||
      yCentre < CENTRE_MIN || yCentre > CENTRE_MAX) {
    Serial.println("  *** CENTRE OUT OF RANGE — expected roughly 1700-2300 ***");
    Serial.println("  Stick touched during calibration? VCC on 3.3 V? Wiring?");
  }
}

static bool stickCentred() {
  return (axisX == 0.0f && axisY == 0.0f);
}

// ----------------------------------------------------------- button helpers --

static void buttonInit(Button &b) {
  pinMode(b.pin, INPUT_PULLUP);

  // Seed from the real pin so a button held through boot is not seen as a fresh
  // press, and pre-fire its long-press so holding it through a reset does
  // nothing either.
  b.rawLast      = (digitalRead(b.pin) == LOW);
  b.stable       = b.rawLast;
  b.lastChangeMs = millis();
  b.pressStartMs = millis();
  b.longFired    = b.stable;
}

static void buttonUpdate(Button &b, uint32_t now) {
  b.pressedEdge  = false;
  b.releasedEdge = false;
  b.longEdge     = false;

  const bool raw = (digitalRead(b.pin) == LOW);
  if (raw != b.rawLast) {
    b.rawLast = raw;
    b.lastChangeMs = now;
  }

  if ((now - b.lastChangeMs) >= DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    if (b.stable) {
      b.pressedEdge  = true;
      b.pressStartMs = now;
      b.longFired    = false;
    } else {
      b.releasedEdge = true;
    }
  }

  if (b.longMs > 0 && b.stable && !b.longFired &&
      (now - b.pressStartMs) >= b.longMs) {
    b.longFired = true;
    b.longEdge  = true;
  }
}

// ------------------------------------------------------------------ events --

/* Both directions need the full hold. The press and early-release messages exist
 * so a short tap reads as "you did not hold long enough" rather than as a dead
 * button — which is otherwise indistinguishable from a wiring fault.
 */
static void handleArm() {
  Button &b = buttons[BTN_ARM];

  if (b.pressedEdge) {
    Serial.printf("[ARM] hold %.0f s to %s...\n",
                  ARM_HOLD_MS / 1000.0f, armed ? "DISARM" : "ARM");
  }

  if (b.releasedEdge && !b.longFired) {
    Serial.println("[ARM] released early — no change");
  }

  if (!b.longEdge) return;

  if (armed) {
    armed = false;
    Serial.println("[ARM] *** DISARMED ***");
    return;
  }

  // Arming onto a deflected stick would command speed the instant the latch
  // closes. Release and hold again once it is centred.
  if (!stickCentred()) {
    Serial.printf("[ARM] refused — centre the stick first (X %+.2f  Y %+.2f)\n",
                  axisX, axisY);
    return;
  }

  armed = true;
  Serial.println("[ARM] *** ARMED ***");
}

static void handleEvents() {
  handleArm();

  if (buttons[BTN_SPEED].pressedEdge) {
    precision = !precision;
    Serial.printf("[SPEED] %s\n", precision ? "PRECISION" : "FULL");
  }

  if (buttons[BTN_CAPTURE].pressedEdge) {
    captureCount++;
    Serial.printf("[CAPTURE] #%lu\n", (unsigned long)captureCount);
  }

  if (buttons[BTN_STICK].longEdge) {
    distanceZeroCount++;
    Serial.printf("[STICK] zero distance #%lu\n",
                  (unsigned long)distanceZeroCount);
  }
}

static void handleSerial() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == 'c' || ch == 'C') calibrate();
  }
}

// ----------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  analogReadResolution(ADC_BITS);
  analogSetAttenuation(ADC_11db);

  for (uint8_t i = 0; i < BTN_COUNT; i++) buttonInit(buttons[i]);

  Serial.println();
  Serial.println("=== Controller stage 2 — joystick + buttons ===");
  Serial.printf("X GPIO%u   Y GPIO%u   ARM GPIO%u   SPEED GPIO%u   "
                "CAPTURE GPIO%u   STICK GPIO%u\n",
                PIN_AXIS_X, PIN_AXIS_Y, buttons[BTN_ARM].pin,
                buttons[BTN_SPEED].pin, buttons[BTN_CAPTURE].pin,
                buttons[BTN_STICK].pin);
  Serial.println("Module VCC must be on 3.3 V.");
  Serial.println();

  // Report anything held at boot, so it is obvious rather than mysterious.
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (buttons[i].stable) {
      Serial.printf("Note: %s is held down at boot — ignored until released.\n",
                    buttons[i].name);
    }
  }

  calibrate();

  Serial.println();
  Serial.println("Ready. Starts DISARMED in PRECISION mode.");
  Serial.println("TX columns are what will go in the ESP-NOW packet at stage 4.");
  Serial.println();
}

// ------------------------------------------------------------------ loop ----

void loop() {
  const uint32_t now = millis();

  handleSerial();

  // Axes are read every pass, not only when printing — the arming interlock
  // needs a current value, and stage 4 will send at 50 Hz.
  rawX = readAveraged(PIN_AXIS_X);
  rawY = readAveraged(PIN_AXIS_Y);

  axisX = normalise(rawX, xCentre);
  axisY = normalise(rawY, yCentre);
  if (INVERT_X) axisX = -axisX;
  if (INVERT_Y) axisY = -axisY;

  for (uint8_t i = 0; i < BTN_COUNT; i++) buttonUpdate(buttons[i], now);
  handleEvents();

  /* What would actually be transmitted. Disarmed sends true zeros rather than
   * relying on the robot to ignore a live value.
   */
  const float scale = armed ? (precision ? PRECISION_SCALE : 1.0f) : 0.0f;
  const int16_t txX = (int16_t)lroundf(axisX * scale * 1000.0f);
  const int16_t txY = (int16_t)lroundf(axisY * scale * 1000.0f);

  if ((now - lastPrintMs) < PRINT_PERIOD_MS) return;
  lastPrintMs = now;

  /* While ARM is held, count the hold up in the status line. Two seconds is long
   * enough that silent waiting feels like nothing is happening.
   */
  char armField[16];
  const Button &a = buttons[BTN_ARM];
  if (a.stable && !a.longFired) {
    snprintf(armField, sizeof armField, "%s %.1fs",
             armed ? "ON" : "off", (now - a.pressStartMs) / 1000.0f);
  } else {
    snprintf(armField, sizeof armField, "%s", armed ? "ON" : "off");
  }

  Serial.printf("X %+.2f (%4u)  Y %+.2f (%4u) | ARM %-8s %-9s | "
                "TX %+5d %+5d | SW %s  CAP %-2lu  ZERO %lu\n",
                axisX, rawX, axisY, rawY,
                armField,
                precision ? "PRECISION" : "FULL",
                txX, txY,
                buttons[BTN_STICK].stable ? "DOWN" : "up  ",
                (unsigned long)captureCount,
                (unsigned long)distanceZeroCount);
}