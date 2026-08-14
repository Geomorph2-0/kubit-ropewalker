/*
 * controller_stage1_joystick.ino — Stage 1 of the controller build.
 *
 * TARGET BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit (the controller,
 * MAC F4:2D:C9:71:7B:0C). This is the classic-ESP32 counterpart to the S3 bench
 * sketch in custom-controller-v1/ — same maths, different pins, one stick
 * instead of two.
 *
 * SCOPE: read one joystick and print it. No buttons, no LEDs, no radio. Those
 * are stages 2, 3 and 4.
 *
 * WIRING
 *
 *   Module pin      Goes to         Header pin
 *   ----------      -------         ----------
 *   GND             GND             14 or 32/38
 *   +5V  (label)    3.3 V           1            <-- NOT 5 V. See below.
 *   VRx             GPIO 32         7            <-- read as the Y axis
 *   VRy             GPIO 33         8            <-- read as the X axis
 *   SW              GPIO 13         15
 *
 * The axes are deliberately crossed in software: on this module the pot marked
 * VRy is the one that moves with left-right motion. See PIN_AXIS_X below. The
 * wiring above is correct as-is — do not swap the wires.
 *
 * THE +5V PIN TAKES 3.3 V. The silkscreen names the rail, nothing more. Inside
 * the module are two 10 kΩ pots wired straight across whatever you feed them,
 * with no series resistor and no clamp. Feed it 5 V and full deflection puts 5 V
 * into a 3.3 V-only pin. You would also lose the top third of travel, because
 * everything above 3.3 V reads as saturated.
 *
 * WHY THESE PINS. GPIO 32 and 33 are ADC1 channels 4 and 5. ADC2 is owned by the
 * WiFi driver and its reads fail once the radio starts — silently, returning
 * garbage rather than an error. ESP-NOW needs that radio from stage 4 onward, so
 * every analog input on this board has to live on ADC1 (GPIO 32-39) from the
 * start. Putting the stick on an ADC2 pin would work perfectly on the bench today
 * and break the moment the link comes up.
 *
 * BOARD SETTINGS
 *   Board            : ESP32 Dev Module
 *   Flash Size       : 4MB (32Mb)
 *   Partition Scheme : Default 4MB with spiffs
 *   Upload Speed     : 921600
 *   Port             : /dev/ttyUSB0  (CP2102 bridge)
 *
 * Close the Serial Monitor before uploading. IDE 2.x has a long-standing bug
 * where compiling with it open blanks the window.
 *
 * EXIT TEST FOR THIS STAGE
 *   1. Centres print between roughly 1700 and 2300 and are stable at rest.
 *   2. Both axes reach +1.00 and -1.00 at full deflection.
 *   3. At rest both axes read exactly 0.00 and do not twitch.
 *   4. Moving slowly out of centre, the value rises smoothly from 0 — no jump.
 *   5. SW reads DOWN when the stick is clicked.
 *
 * SERIAL COMMANDS (type the letter, press Enter)
 *   c   recalibrate centre — hands off the stick first
 *   r   reset the min/max travel tracker
 */

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------- config ----

/* Named for the axis they DRIVE, not for the module's silkscreen.
 *
 * The wiring is unchanged: the module's VRx pin is still on GPIO 32 and VRy on
 * GPIO 33. On this stick it is the pot labelled VRy that responds to left-right
 * motion, so that is the one feeding X. The silkscreen describes the module's
 * own orientation, not how it ends up mounted — swapping here rather than
 * swapping wires keeps the physical build untouched.
 */
static const uint8_t PIN_AXIS_X = 33;   // module VRy — ADC1_CH5 — left / right
static const uint8_t PIN_AXIS_Y = 32;   // module VRx — ADC1_CH4 — up / down
static const uint8_t PIN_SW     = 13;   // digital, internal pull-up

/* Sign, as opposed to which pot. Whether pushing right reads positive depends on
 * which way round that pot happens to be wired. Push right and up; if either
 * sign is backwards, flip the matching flag. Set by experiment, not by reasoning.
 */
static const bool INVERT_X = false;
static const bool INVERT_Y = false;

static const uint8_t  ADC_BITS        = 12;    // 0..4095
static const uint16_t ADC_MAX         = 4095;
static const uint8_t  OVERSAMPLE      = 8;     // averaged per read
static const uint16_t CAL_SAMPLES     = 64;    // averaged at boot
static const float    DEADZONE        = 0.06f; // fraction of full travel
static const uint32_t PRINT_PERIOD_MS = 100;   // 10 Hz

/* A plausible resting centre. Outside this band, something is wrong: the stick
 * was held during calibration, a wire is off, or the module is on the wrong
 * rail. Worth catching now rather than debugging it as "drift" in stage 4.
 */
static const uint16_t CENTRE_MIN = 1200;
static const uint16_t CENTRE_MAX = 2900;

// ----------------------------------------------------------------- state ----

static uint16_t xCentre = 2048, yCentre = 2048;
static uint16_t xMin = ADC_MAX, xMax = 0;
static uint16_t yMin = ADC_MAX, yMax = 0;
static uint32_t lastPrintMs = 0;

// --------------------------------------------------------------- helpers ----

// Cheap pots and the ESP32 ADC together give a few tens of counts of jitter.
// Averaging costs microseconds and removes most of it.
static uint16_t readAveraged(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) {
    sum += (uint32_t)analogRead(pin);
  }
  return (uint16_t)(sum / OVERSAMPLE);
}

/* Map a raw reading to -1.0 .. +1.0 around a measured centre.
 *
 * Centre is almost never 2048. Mechanical tolerance and ADC nonlinearity put it
 * anywhere from roughly 1700 to 2300. Scaling each side against its own span
 * means full deflection still reaches 1.0 in both directions on an off-centre
 * stick, instead of hitting 0.85 one way and clipping the other.
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

  // Rescale outside the deadzone so there is no discontinuity at its edge.
  // Without this the stick visibly jumps from 0 to 0.06 as it leaves centre.
  if (fabsf(v) < DEADZONE) return 0.0f;
  const float sign = (v > 0.0f) ? 1.0f : -1.0f;
  return sign * (fabsf(v) - DEADZONE) / (1.0f - DEADZONE);
}

static void resetRange() {
  xMin = ADC_MAX; xMax = 0;
  yMin = ADC_MAX; yMax = 0;
  Serial.println("Travel range reset. Sweep the stick to its limits.");
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

  const bool xBad = (xCentre < CENTRE_MIN || xCentre > CENTRE_MAX);
  const bool yBad = (yCentre < CENTRE_MIN || yCentre > CENTRE_MAX);
  if (xBad || yBad) {
    Serial.println();
    Serial.println("  *** CENTRE OUT OF RANGE ***");
    Serial.println("  Expected roughly 1700-2300 at rest. Check, in order:");
    Serial.println("    - stick was touched during calibration");
    Serial.println("    - module VCC on 3.3 V, not 5 V or floating");
    Serial.println("    - VRx/VRy actually on GPIO 32 / 33");
    Serial.println("    - GND shared with the board");
    Serial.println("  A reading pinned near 0 or 4095 means an open or a short.");
    Serial.println();
  }
}

static void handleSerial() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == 'c' || ch == 'C') calibrate();
    if (ch == 'r' || ch == 'R') resetRange();
  }
}

// ----------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  analogReadResolution(ADC_BITS);

  /* Full-scale attenuation, so the usable input range covers roughly 0-3.3 V.
   * Without it the ADC saturates near 1.1 V and the stick appears to hit its
   * limit a third of the way through its travel.
   */
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_SW, INPUT_PULLUP);

  Serial.println();
  Serial.println("=== Controller stage 1 — joystick ===");
  Serial.printf("X GPIO%u (module VRy)   Y GPIO%u (module VRx)   SW GPIO%u\n",
                PIN_AXIS_X, PIN_AXIS_Y, PIN_SW);
  Serial.printf("Resolution %u-bit (0-%u), oversample %u, deadzone %.0f%%\n",
                ADC_BITS, ADC_MAX, OVERSAMPLE, DEADZONE * 100.0f);
  Serial.println("Module VCC must be on 3.3 V.");
  Serial.println();

  calibrate();
  resetRange();

  Serial.println();
  Serial.println("Ready. Axes read -1.00 to +1.00.");
  Serial.println("Bracketed figures are the min/max seen so far — sweep to both");
  Serial.println("limits and check they approach 0 and 4095.");
  Serial.println("Keys:  c = recalibrate centre   r = reset range");
  Serial.println();
}

// ------------------------------------------------------------------ loop ----

void loop() {
  handleSerial();

  const uint32_t now = millis();
  if ((now - lastPrintMs) < PRINT_PERIOD_MS) return;
  lastPrintMs = now;

  const uint16_t rawX = readAveraged(PIN_AXIS_X);
  const uint16_t rawY = readAveraged(PIN_AXIS_Y);

  if (rawX < xMin) xMin = rawX;
  if (rawX > xMax) xMax = rawX;
  if (rawY < yMin) yMin = rawY;
  if (rawY > yMax) yMax = rawY;

  float x = normalise(rawX, xCentre);
  float y = normalise(rawY, yCentre);
  if (INVERT_X) x = -x;
  if (INVERT_Y) y = -y;

  const bool pressed = (digitalRead(PIN_SW) == LOW);

  /* The int16 columns are what will travel in the ESP-NOW control packet from
   * stage 4 (-1000..+1000). Printing them now means the packet fields have
   * already been eyeballed before there is a radio to blame.
   */
  const int16_t xPkt = (int16_t)lroundf(x * 1000.0f);
  const int16_t yPkt = (int16_t)lroundf(y * 1000.0f);

  // Raw counts sit beside the normalised value so a stuck or miswired channel
  // is obvious at a glance rather than looking like a calibration problem.
  Serial.printf("X %+.2f (%4u) [%4u..%4u] %+5d   "
                "Y %+.2f (%4u) [%4u..%4u] %+5d   SW %s\n",
                x, rawX, xMin, xMax, xPkt,
                y, rawY, yMin, yMax, yPkt,
                pressed ? "DOWN" : "up  ");
}