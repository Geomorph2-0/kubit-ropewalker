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

// 3) Camera board pinout: 0 = XIAO ESP32S3 Sense, 1 = DevKitC-1 + OV2640 breakout
#define CAM_BOARD 0

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
#define HQ_JPEG_QUALITY 10       // quality for saved victim picture (lower = better)
#define SERIAL_BAUD 115200

#endif  // CONFIG_MODEL_H
