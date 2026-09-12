#ifndef CONFIG_MODEL_H
#define CONFIG_MODEL_H

// ============================================================
// EDIT THESE 4 LINES TO SWITCH MODEL
// Default: mobilenetv3_96, THRESHOLD 0.65
// ============================================================

// 1) Pick model: 96 (default) or 128
#define MODEL_W 96
#define MODEL_H 96
#define BACKBONE "mobilenetv3_96"

// To use 128 instead, comment the 3 lines above and uncomment:
// #define MODEL_W 128
// #define MODEL_H 128
// #define BACKBONE "mobilenetv3_128"

// 2) Decision threshold (sigmoid 0..1). Default 0.65 = fewer false alarms.
//    mobilenetv3_96 test: prec 0.84 / rec 0.875 at 0.5; 0.65 trades recall for precision.
//    Lower to 0.5 for max recall, raise to 0.7 for max precision.
#define THRESHOLD 0.65f

// 3) Board: fixed to Freenove ESP32-S3 WROOM (N16R8) + OV3660 camera.
//    Camera pins match Espressif's CAMERA_MODEL_ESP32S3_EYE layout, as used
//    by Freenove's own Sketch_07.3_Camera_SDcard example for this board.
#define CAM_XCLK_GPIO   15
#define CAM_SIOD_GPIO    4
#define CAM_SIOC_GPIO    5
#define CAM_Y2_GPIO     11
#define CAM_Y3_GPIO      9
#define CAM_Y4_GPIO      8
#define CAM_Y5_GPIO     10
#define CAM_Y6_GPIO     12
#define CAM_Y7_GPIO     18
#define CAM_Y8_GPIO     17
#define CAM_Y9_GPIO     16
#define CAM_VSYNC_GPIO   6
#define CAM_HREF_GPIO    7
#define CAM_PCLK_GPIO   13
#define CAM_PWDN_GPIO   -1
#define CAM_RESET_GPIO  -1

// SD card (SDMMC, 1-bit) — fixed pins per Freenove's own board reference
// (sd_read_write.h: "Please do not modify it"). Do not change without
// confirming against Freenove's documentation.
#define SD_CLK_GPIO 39
#define SD_CMD_GPIO 38
#define SD_D0_GPIO  40

// 4) Tensor arena — sized from on-device arena_used_bytes() (worst stack + headroom).
//    mobilenetv3_96: 219KB (IDF) / 318KB (Arduino) -> 384KB here.
//    mobilenetv3_128: 315KB (IDF) / 510KB (Arduino) -> 640KB here.
#if MODEL_W == 96 && MODEL_H == 96
constexpr int kTensorArenaSize = 384 * 1024;
#elif MODEL_W == 128 && MODEL_H == 128
constexpr int kTensorArenaSize = 640 * 1024;
#else
#error "Only 96x96 and 128x128 are supported in this pack"
#endif

// Capture settings
#define LOOP_DELAY_MS 200        // pause between inferences

#endif  // CONFIG_MODEL_H
