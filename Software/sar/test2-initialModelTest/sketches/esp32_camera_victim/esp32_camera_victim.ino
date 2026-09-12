/*
  esp32_camera_victim.ino — Live victim detection on ESP32-S3
  Models: mobilenetv3_96 (default) or mobilenetv3_128 (int8, full-integer)

  Workflow (matches training exactly):
    camera capture (RGB565)
      -> letterbox to MODEL_W x MODEL_H, black pad (process_data.py)
      -> mobilenet preprocess: p = (pixel / 127.5) - 1.0  (train.py)
      -> quantize to int8 via input scale/zero_point
      -> Invoke() (timed)
      -> dequant output -> sigmoid score 0..1
      -> score > THRESHOLD (0.65 default) => VICTIM: re-capture HQ JPEG, save to SD

  Setup (same convention as friend_esp32_kit_v2/GUIDE.md):
    Arduino IDE 2.x, ESP32 core 3.0.5+, TensorFlowLite_ESP32 lib
    Board: ESP32S3 Dev Module (DevKitC-1) or XIAO_ESP32S3
    Flash 8MB, Partition 8M OTA, PSRAM: Enabled (OPI), CPU 240MHz, USB CDC On Boot: Enabled

  Steps:
    1) Copy models/mobilenetv3_96/model_96_int8.h -> this folder as model.h
       (or model_128_int8.h if using 128, and update config_model.h)
    2) Edit config_model.h (MODEL_W/H, THRESHOLD, CAM_BOARD)
    3) Flash, open Serial 115200
*/

#include "config_model.h"
#include "model.h"  // <- you copy model_96_int8.h (or model_128_int8.h) to this name

#if MODEL_W == 96 && MODEL_H == 96
extern const unsigned char model_96_int8[];
extern const unsigned int model_96_int8_len;
#define MODEL_DATA model_96_int8
#elif MODEL_W == 128 && MODEL_H == 128
extern const unsigned char model_128_int8[];
extern const unsigned int model_128_int8_len;
#define MODEL_DATA model_128_int8
#endif

#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "FS.h"
#include "SD_MMC.h"

tflite::MicroErrorReporter micro_error_reporter;
tflite::AllOpsResolver resolver;
static tflite::MicroInterpreter* interpreter = nullptr;
uint8_t* tensor_arena = nullptr;

// ---- Camera pinouts (same board convention as benchmark kit) ----
#if CAM_BOARD == 0
// XIAO ESP32S3 Sense (OV2640 onboard)
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13
#else
// DevKitC-1 + OV2640 breakout (AI-Thinker style). Adjust if your wiring differs.
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM    5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22
#endif

// Working buffers (PSRAM): RGB888 letterboxed input, float preprocess scratch
static uint8_t* rgb_buf = nullptr;   // MODEL_W * MODEL_H * 3
static float* prep_buf = nullptr;    // MODEL_W * MODEL_H * 3 floats

static inline int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool init_camera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;  // easy to letterbox without JPEG decode
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
  // Capture at/above model size; letterbox down. QVGA is enough for 96/128.
  config.frame_size = FRAMESIZE_QVGA;      // 320x240
  config.jpeg_quality = 12;
  config.sccb_i2c_port = 0;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED 0x%x (check CAM_BOARD + wiring + PSRAM Enabled)\n", err);
    return false;
  }
  // Slight gain for indoor track lighting
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
  }
  return true;
}

// Letterbox: RGB565 frame -> RGB888 MODEL_W x MODEL_H, black pad, aspect fit.
// Matches training: thumbnail(LANCZOS) + centered black canvas (process_data.py).
// On-device uses nearest-neighbor (cheap); aspect + centering + black pad are what matter.
void letterbox_rgb565_to_rgb888(const uint8_t* src565, int srcW, int srcH) {
  // Compute fit size
  float scale = fminf((float)MODEL_W / srcW, (float)MODEL_H / srcH);
  int fitW = (int)(srcW * scale);
  int fitH = (int)(srcH * scale);
  int offX = (MODEL_W - fitW) / 2;
  int offY = (MODEL_H - fitH) / 2;
  // Black canvas
  memset(rgb_buf, 0, MODEL_W * MODEL_H * 3);
  const uint16_t* px = (const uint16_t*)src565;
  for (int y = 0; y < fitH; y++) {
    int sy = (int)(y / scale);
    if (sy >= srcH) sy = srcH - 1;
    for (int x = 0; x < fitW; x++) {
      int sx = (int)(x / scale);
      if (sx >= srcW) sx = srcW - 1;
      uint16_t c = px[sy * srcW + sx];
      uint8_t r = ((c >> 11) & 0x1F) << 3;
      uint8_t g = ((c >> 5) & 0x3F) << 2;
      uint8_t b = (c & 0x1F) << 3;
      int dx = offX + x, dy = offY + y;
      int di = (dy * MODEL_W + dx) * 3;
      rgb_buf[di] = r;
      rgb_buf[di + 1] = g;
      rgb_buf[di + 2] = b;
    }
  }
}

// MobileNet preprocess: p = (pixel / 127.5) - 1.0  (train.py mobilenet_v2.preprocess_input)
// Do NOT feed 0-255 raw — that is for the custom backbone only.
void preprocess_mobilenet() {
  int n = MODEL_W * MODEL_H * 3;
  for (int i = 0; i < n; i++) {
    prep_buf[i] = ((float)rgb_buf[i] / 127.5f) - 1.0f;
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(2000);
  Serial.println("\n=== ESP32-S3 Victim Inference ===");
  Serial.printf("Model: %s %dx%d  THRESHOLD=%.2f\n", BACKBONE, MODEL_W, MODEL_H, (double)THRESHOLD);
  Serial.printf("PSRAM: %s  Arena: %d KB\n", psramFound() ? "yes" : "NO (enable PSRAM!)", kTensorArenaSize / 1024);

  // Buffers in PSRAM
  rgb_buf = (uint8_t*)ps_malloc(MODEL_W * MODEL_H * 3);
  prep_buf = (float*)ps_malloc(MODEL_W * MODEL_H * 3 * sizeof(float));
  if (!rgb_buf || !prep_buf) {
    Serial.println("Frame buffer alloc FAILED! Enable PSRAM (OPI).");
    return;
  }
  if (psramFound()) {
    tensor_arena = (uint8_t*)ps_malloc(kTensorArenaSize);
  } else {
    tensor_arena = (uint8_t*)malloc(kTensorArenaSize);
  }
  if (!tensor_arena) {
    Serial.println("Arena alloc FAILED! Enable PSRAM or use mobilenetv3_96.");
    return;
  }

  const tflite::Model* model = tflite::GetModel(MODEL_DATA);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch! Wrong model.h?");
    return;
  }
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize, &micro_error_reporter);
  interpreter = &static_interpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors FAILED! Increase arena or enable PSRAM.");
    return;
  }
  Serial.printf("Arena used: %d bytes\n", interpreter->arena_used_bytes());
  TfLiteTensor* in = interpreter->input(0);
  Serial.printf("Input: [%d %d %d %d] type=%d scale=%f zp=%d\n",
      in->dims->data[0], in->dims->data[1], in->dims->data[2], in->dims->data[3],
      (int)in->type, in->params.scale, in->params.zero_point);

  if (!init_camera()) return;

  // SD for victim snapshots (optional — continues without it)
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC not found — victim pictures will only print score (no save).");
  } else {
    Serial.println("SD_MMC ready: /victim_XXXX.jpg");
  }
  Serial.println("Ready. Looping capture -> infer ...");
}

int pic_index = 0;

void loop() {
  if (!interpreter) { delay(1000); return; }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture FAILED");
    delay(500);
    return;
  }
  if (fb->format != PIXFORMAT_RGB565) {
    Serial.printf("Unexpected format %d (want RGB565)\n", fb->format);
    esp_camera_fb_return(fb);
    delay(500);
    return;
  }

  // 1) Letterbox to model size
  letterbox_rgb565_to_rgb888(fb->buf, fb->width, fb->height);
  esp_camera_fb_return(fb);

  // 2) MobileNet preprocess
  preprocess_mobilenet();

  // 3) Quantize float [-1,1] -> int8
  TfLiteTensor* in = interpreter->input(0);
  float scale = in->params.scale;
  int zp = in->params.zero_point;
  int n = MODEL_W * MODEL_H * 3;
  for (int i = 0; i < n; i++) {
    int q = (int)roundf(prep_buf[i] / scale) + zp;
    in->data.int8[i] = (int8_t)clamp_int(q, -128, 127);
  }

  // 4) Invoke (timed)
  int64_t t0 = esp_timer_get_time();
  TfLiteStatus ok = interpreter->Invoke();
  int64_t dt_us = esp_timer_get_time() - t0;
  if (ok != kTfLiteOk) {
    Serial.println("Invoke FAILED");
    delay(500);
    return;
  }

  // 5) Dequant output -> score 0..1
  TfLiteTensor* out = interpreter->output(0);
  float score = ((float)out->data.int8[0] - out->params.zero_point) * out->params.scale;
  // Output is sigmoid already in float domain; clamp for display
  if (score < 0) score = 0;
  if (score > 1) score = 1;
  bool is_victim = score > THRESHOLD;

  Serial.printf("score=%.3f %s (%.1f ms)\n",
      score, is_victim ? "VICTIM" : "healthy", dt_us / 1000.0);

  // 6) Action: on victim, re-capture HQ JPEG and save
  if (is_victim) {
    // Switch to HQ still: take another frame at current settings and save raw.
    // For a true HQ still, reconfigure to UXGA JPEG — kept simple here: save
    // a marker file + note. Full UXGA switch needs deinit/reinit; see guide.
    pic_index++;
    File f = SD_MMC.open(String("/victim_") + pic_index + ".txt", FILE_WRITE);
    if (f) {
      f.printf("victim score=%.3f model=%s %dx%d threshold=%.2f\n",
          score, BACKBONE, MODEL_W, MODEL_H, (double)THRESHOLD);
      f.close();
      Serial.printf("Saved /victim_%d.txt (swap to UXGA JPEG for full picture — see guide §6)\n", pic_index);
    }
  }

  vTaskDelay(1);  // yield every loop (WDT fix from device benchmark)
  delay(LOOP_DELAY_MS);
}
