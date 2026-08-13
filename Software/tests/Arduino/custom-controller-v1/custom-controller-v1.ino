                                                          /*
 * dual_joystick_s3.ino — read two analog joystick modules (KY-023 style) on an
 * ESP32-S3 and print normalised axis values over serial.
 *
 * TARGET BOARD: Freenove ESP32-S3-WROOM CAM (N16R8). This is the bench-test
 * version. For the classic ESP32 build, see dual_joystick.ino — the pin
 * constraints on the two chips are different enough that they are separate
 * sketches rather than one with an #ifdef.
 *
 * WIRING (both modules share the 3.3 V and GND rails)
 *
 *   Module pin      Joystick 1      Joystick 2
 *   ----------      ----------      ----------
 *   GND             GND             GND
 *   +5V  (label)    3.3 V           3.3 V      <-- NOT 5 V. See below.
 *   VRx             GPIO 1          GPIO 10
 *   VRy             GPIO 8          GPIO 9
 *   SW              GPIO 41         GPIO 42
 *
 * DO NOT use GPIO 2 for an analog input on this board. The onboard blue LED
 * sits on that pin; above roughly 2.7 V it starts conducting and draws current
 * out of the pot wiper, which is a high-impedance source. The result is a
 * compressed, non-linear top end that looks like the stick running out of
 * travel early. GPIO 8 is ADC1, has no LED and is not a strapping pin.
 *
 * The module is two 10k pots wired across VCC. Whatever sits on VCC appears on
 * VRx/VRy at full deflection, and there is no series resistor to protect the
 * ADC pin. 5 V on VCC means 5 V into a 3.3 V input.
 *
 * DISCONNECT THE CAMERA RIBBON before running this. GPIO 9 and 10 are camera
 * data lines on this board. All four axes are deliberately on ADC1 (GPIO 1-10)
 * because ADC2 returns garbage whenever WiFi is active.
 *
 * BOARD SETTINGS
 *   Board           : ESP32S3 Dev Module
 *   PSRAM           : OPI PSRAM
 *   Flash Size      : 16MB (128Mb)
 *   USB CDC On Boot : Disabled — monitor on the UART bridge port
 */

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------- config ----

/* xPin / yPin are defined by OBSERVED motion, not by the module silkscreen.
 * The VRx/VRy labels describe the axes relative to the module's own
 * orientation, so rotating the module on the breadboard swaps them. Whichever
 * pot responds to left-right motion is X here, whatever the PCB calls it.
 *
 * invertX / invertY flip the sign so that right and up read positive. Which
 * way round a given pot is wired is arbitrary, so expect to set these by
 * experiment rather than by reasoning about it.
 */
struct JoystickPins {
  uint8_t xPin;
  uint8_t yPin;
  uint8_t swPin;
  bool    invertX;
  bool    invertY;
  const char *label;
};

static const JoystickPins kSticks[] = {
  // J1 X was moved to GPIO 8: GPIO 2 carries the onboard blue LED, which
  // conducts above ~2.7 V and loads the wiper, clamping the top of the range.
  {  8,  1, 41, false, false, "J1" },
  { 10,  9, 42, false, false, "J2" },
};

static const size_t kStickCount = sizeof(kSticks) / sizeof(kSticks[0]);

static const uint8_t  ADC_BITS        = 12;    // 0..4095
static const uint16_t ADC_MAX         = 4095;
static const uint8_t  OVERSAMPLE      = 8;     // samples averaged per read
static const uint16_t CAL_SAMPLES     = 64;    // samples taken at boot
static const float    DEADZONE        = 0.06f; // fraction of full travel
static const uint32_t PRINT_PERIOD_MS = 100;   // 10 Hz

// ----------------------------------------------------------------- state ----

struct StickCal {
  uint16_t xCentre;
  uint16_t yCentre;
};

static StickCal cal[kStickCount];
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
 * Centre is almost never 2048. Mechanical tolerance and ADC nonlinearity put
 * it anywhere from roughly 1700 to 2300, and two sticks from the same bag will
 * differ. Scaling each side against its own span means full deflection still
 * reaches 1.0 in both directions on an off-centre stick.
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
  // Without this the stick appears to jump as it leaves centre.
  if (fabsf(v) < DEADZONE) return 0.0f;
  const float sign = (v > 0.0f) ? 1.0f : -1.0f;
  return sign * (fabsf(v) - DEADZONE) / (1.0f - DEADZONE);
}

static void calibrate() {
  Serial.println("Calibrating — do not touch the sticks...");
  delay(500);

  for (size_t i = 0; i < kStickCount; i++) {
    uint32_t xSum = 0, ySum = 0;
    for (uint16_t s = 0; s < CAL_SAMPLES; s++) {
      xSum += (uint32_t)analogRead(kSticks[i].xPin);
      ySum += (uint32_t)analogRead(kSticks[i].yPin);
      delay(2);
    }
    cal[i].xCentre = (uint16_t)(xSum / CAL_SAMPLES);
    cal[i].yCentre = (uint16_t)(ySum / CAL_SAMPLES);

    Serial.printf("  %s centre: X=%u  Y=%u\n",
                  kSticks[i].label, cal[i].xCentre, cal[i].yCentre);
  }
  Serial.println();
}

// ----------------------------------------------------------------- setup ----

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  analogReadResolution(ADC_BITS);

  // Full-scale attenuation so the usable input range covers roughly 0-3.3 V.
  // Without it the ADC saturates near 1.1 V and the sticks look like they hit
  // their limit a third of the way through their travel.
  analogSetAttenuation(ADC_11db);

  for (size_t i = 0; i < kStickCount; i++) {
    pinMode(kSticks[i].swPin, INPUT_PULLUP);
  }

  Serial.println();
  Serial.println("=== Dual joystick monitor (ESP32-S3) ===");
  Serial.printf("Resolution: %u-bit (0-%u)\n", ADC_BITS, ADC_MAX);
  Serial.println("Camera ribbon must be disconnected — GPIO 9/10 are shared.");
  Serial.println();

  calibrate();
  Serial.println("Ready. Axis values run -1.00 to +1.00.");
  Serial.println();
}

// ------------------------------------------------------------------ loop ----

void loop() {
  const uint32_t now = millis();
  if ((now - lastPrintMs) < PRINT_PERIOD_MS) return;
  lastPrintMs = now;

  for (size_t i = 0; i < kStickCount; i++) {
    const uint16_t rawX = readAveraged(kSticks[i].xPin);
    const uint16_t rawY = readAveraged(kSticks[i].yPin);

    float x = normalise(rawX, cal[i].xCentre);
    float y = normalise(rawY, cal[i].yCentre);
    if (kSticks[i].invertX) x = -x;
    if (kSticks[i].invertY) y = -y;

    const bool pressed = (digitalRead(kSticks[i].swPin) == LOW);

    // Raw counts are printed alongside the normalised value so a stuck or
    // miswired channel is obvious at a glance.
    Serial.printf("%s  X %+.2f (%4u)  Y %+.2f (%4u)  SW %s    ",
                  kSticks[i].label, x, rawX, y, rawY,
                  pressed ? "DOWN" : "up  ");
  }
  Serial.println();
}
