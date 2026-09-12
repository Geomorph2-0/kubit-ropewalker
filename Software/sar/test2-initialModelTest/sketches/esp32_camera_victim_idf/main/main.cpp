/*
  main.cpp — ESP-IDF port of the "Victim vs NotVictim" camera inference firmware
  Board: Freenove ESP32-S3 WROOM (N16R8), OV3660 camera, SDMMC SD card
  Models: mobilenetv3_96 (default) or mobilenetv3_128 (int8, full-integer)

  This is the ESP-IDF counterpart of
  sketches/esp32_camera_victim/esp32_camera_victim.ino. Camera pins, SD pins,
  and the letterbox/preprocess/quantize/dequant math are ported to match that
  sketch exactly for this board; see config_model.h for the pin definitions.

  Workflow (matches training exactly):
    camera capture (RGB565)
      -> letterbox to MODEL_W x MODEL_H, black pad (process_data.py)
      -> mobilenet preprocess: p = (pixel / 127.5) - 1.0  (train.py)
      -> quantize to int8 via input scale/zero_point
      -> Invoke() (timed)
      -> dequant output -> sigmoid score 0..1
      -> score > THRESHOLD (0.65 default) => VICTIM: save marker to SD

  Build (venv already active via the `esp-idf` shell alias):
    idf.py set-target esp32s3
    idf.py build
    idf.py -p <PORT> flash monitor
*/

#include "config_model.h"

#if MODEL_W == 96 && MODEL_H == 96
#include "model_96_int8.h"
#define MODEL_DATA model_96_int8
#elif MODEL_W == 128 && MODEL_H == 128
#include "model_128_int8.h"
#define MODEL_DATA model_128_int8
#endif

#include <cstdio>
#include <cstring>
#include <cmath>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_camera.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static uint8_t* tensor_arena = nullptr;
static tflite::MicroInterpreter* interpreter = nullptr;

// Working buffers (PSRAM): RGB888 letterboxed input, float preprocess scratch
static uint8_t* rgb_buf = nullptr;   // MODEL_W * MODEL_H * 3
static float* prep_buf = nullptr;    // MODEL_W * MODEL_H * 3 floats

static sdmmc_card_t* sd_card = nullptr;
static bool sd_ready = false;

static inline int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static bool init_camera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2_GPIO;
  config.pin_d1 = CAM_Y3_GPIO;
  config.pin_d2 = CAM_Y4_GPIO;
  config.pin_d3 = CAM_Y5_GPIO;
  config.pin_d4 = CAM_Y6_GPIO;
  config.pin_d5 = CAM_Y7_GPIO;
  config.pin_d6 = CAM_Y8_GPIO;
  config.pin_d7 = CAM_Y9_GPIO;
  config.pin_xclk = CAM_XCLK_GPIO;
  config.pin_pclk = CAM_PCLK_GPIO;
  config.pin_vsync = CAM_VSYNC_GPIO;
  config.pin_href = CAM_HREF_GPIO;
  config.pin_sccb_sda = CAM_SIOD_GPIO;
  config.pin_sccb_scl = CAM_SIOC_GPIO;
  config.pin_pwdn = CAM_PWDN_GPIO;
  config.pin_reset = CAM_RESET_GPIO;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;  // easy to letterbox without JPEG decode
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
  config.frame_size = FRAMESIZE_QVGA;      // 320x240; letterbox down to model size
  config.jpeg_quality = 12;
  config.sccb_i2c_port = 0;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    printf("Camera init FAILED 0x%x (check wiring + PSRAM Enabled)\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // OV3660-specific orientation (from Freenove's own camera+SD example for
    // this board) — different from OV2640's vflip convention.
    if (s->id.PID == OV3660_PID) {
      s->set_hmirror(s, 1);
      s->set_vflip(s, 0);
    }
    s->set_brightness(s, 1);   // slight gain for indoor lighting
    s->set_saturation(s, 0);
  }
  return true;
}

// Optional: mounts /sdcard over 1-bit SDMMC. Non-fatal on failure — the
// firmware keeps running and printing scores without saving victim markers.
static bool mount_sd() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk = (gpio_num_t)SD_CLK_GPIO;
  slot_config.cmd = (gpio_num_t)SD_CMD_GPIO;
  slot_config.d0 = (gpio_num_t)SD_D0_GPIO;
  slot_config.width = 1;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config,
                                           &mount_config, &sd_card);
  if (err != ESP_OK) {
    printf("SD mount FAILED (0x%x) — continuing without SD save\n", err);
    return false;
  }
  return true;
}

// Letterbox: RGB565 frame -> RGB888 MODEL_W x MODEL_H, black pad, aspect fit.
// Matches training: thumbnail(LANCZOS) + centered black canvas (process_data.py).
// On-device uses nearest-neighbor (cheap); aspect + centering + black pad are what matter.
static void letterbox_rgb565_to_rgb888(const uint8_t* src565, int srcW, int srcH) {
  float scale = fminf((float)MODEL_W / srcW, (float)MODEL_H / srcH);
  int fitW = (int)(srcW * scale);
  int fitH = (int)(srcH * scale);
  int offX = (MODEL_W - fitW) / 2;
  int offY = (MODEL_H - fitH) / 2;
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
static void preprocess_mobilenet() {
  int n = MODEL_W * MODEL_H * 3;
  for (int i = 0; i < n; i++) {
    prep_buf[i] = ((float)rgb_buf[i] / 127.5f) - 1.0f;
  }
}

static void victim_task(void*) {
  printf("\n=== ESP32-S3 Victim Inference ===\n");
  printf("Model: %s %dx%d  THRESHOLD=%.2f\n", BACKBONE, MODEL_W, MODEL_H, (double)THRESHOLD);

  rgb_buf = (uint8_t*)heap_caps_malloc(MODEL_W * MODEL_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  prep_buf = (float*)heap_caps_malloc(MODEL_W * MODEL_H * 3 * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rgb_buf || !prep_buf) {
    printf("Frame buffer alloc FAILED! Enable PSRAM.\n");
    vTaskDelete(nullptr);
    return;
  }

  tensor_arena = (uint8_t*)heap_caps_malloc_prefer(
      kTensorArenaSize, 2,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  printf("PSRAM: %s  Arena: %d KB\n",
         tensor_arena && esp_ptr_external_ram(tensor_arena) ? "yes" : "NO (enable PSRAM!)",
         kTensorArenaSize / 1024);
  if (!tensor_arena) {
    printf("Arena alloc FAILED!\n");
    vTaskDelete(nullptr);
    return;
  }

  const tflite::Model* model = tflite::GetModel(MODEL_DATA);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    printf("Model schema mismatch! Wrong model header?\n");
    vTaskDelete(nullptr);
    return;
  }

  // Exact op set proven against these byte-identical model files in
  // friend_esp32_kit_v2/bench/main/main.cpp. If AllocateTensors() ever fails
  // logging "Didn't find op for builtin opcode 'X'", add resolver.AddX().
  static tflite::MicroMutableOpResolver<7> resolver;
  resolver.AddAdd();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddLogistic();
  resolver.AddMaxPool2D();
  resolver.AddMean();

  static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    printf("AllocateTensors FAILED! Increase arena.\n");
    vTaskDelete(nullptr);
    return;
  }
  printf("Arena used: %u bytes\n", (unsigned)interpreter->arena_used_bytes());
  TfLiteTensor* in = interpreter->input(0);
  printf("Input: [%d %d %d %d] type=%d scale=%f zp=%ld\n",
         in->dims->data[0], in->dims->data[1], in->dims->data[2], in->dims->data[3],
         (int)in->type, in->params.scale, (long)in->params.zero_point);

  if (!init_camera()) {
    vTaskDelete(nullptr);
    return;
  }
  printf("Camera init OK\n");

  sd_ready = mount_sd();
  printf("%s\n", sd_ready ? "SD mount OK" : "SD not found — victim pictures will only print score (no save).");

  printf("Ready. Looping capture -> infer ...\n");

  int pic_index = 0;
  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      printf("Capture FAILED\n");
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (fb->format != PIXFORMAT_RGB565) {
      printf("Unexpected format %d (want RGB565)\n", fb->format);
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // 1) Letterbox to model size
    letterbox_rgb565_to_rgb888(fb->buf, fb->width, fb->height);
    esp_camera_fb_return(fb);

    // 2) MobileNet preprocess
    preprocess_mobilenet();

    // 3) Quantize float [-1,1] -> int8
    TfLiteTensor* in2 = interpreter->input(0);
    float scale = in2->params.scale;
    int zp = in2->params.zero_point;
    int n = MODEL_W * MODEL_H * 3;
    for (int i = 0; i < n; i++) {
      int q = (int)roundf(prep_buf[i] / scale) + zp;
      in2->data.int8[i] = (int8_t)clamp_int(q, -128, 127);
    }

    // 4) Invoke (timed)
    int64_t t0 = esp_timer_get_time();
    TfLiteStatus ok = interpreter->Invoke();
    int64_t dt_us = esp_timer_get_time() - t0;
    if (ok != kTfLiteOk) {
      printf("Invoke FAILED\n");
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // 5) Dequant output -> score 0..1
    TfLiteTensor* out = interpreter->output(0);
    float score = ((float)out->data.int8[0] - out->params.zero_point) * out->params.scale;
    if (score < 0) score = 0;
    if (score > 1) score = 1;
    bool is_victim = score > THRESHOLD;

    printf("score=%.3f %s (%.1f ms)\n", score, is_victim ? "VICTIM" : "healthy", dt_us / 1000.0);

    // 6) Action: on victim, save marker file to SD
    if (is_victim && sd_ready) {
      pic_index++;
      char path[32];
      snprintf(path, sizeof(path), "/sdcard/victim_%d.txt", pic_index);
      FILE* f = fopen(path, "w");
      if (f) {
        fprintf(f, "victim score=%.3f model=%s %dx%d threshold=%.2f\n",
                score, BACKBONE, MODEL_W, MODEL_H, (double)THRESHOLD);
        fclose(f);
        printf("Saved %s\n", path);
      }
    }

    vTaskDelay(1);  // yield every loop (WDT fix, matches on-device benchmark)
    vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
  }
}

extern "C" void app_main(void) {
  // Dedicated task with a large stack — the default main-task stack
  // (CONFIG_ESP_MAIN_TASK_STACK_SIZE) is too small for AllocateTensors()/
  // Invoke() plus the camera driver's own buffers and our letterbox/preprocess
  // scratch (matches bench's documented fix, sized up for the camera+SD load).
  xTaskCreate(victim_task, "victim", 12288, nullptr, 5, nullptr);
}
