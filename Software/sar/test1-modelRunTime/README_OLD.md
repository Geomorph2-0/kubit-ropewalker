# Friend ESP32-S3 Benchmark Kit

You received 12 int8 models (6 resolutions × 2 backbones). This kit lets you measure **on-device** Invoke() time without needing Python/TensorFlow.

## Folder
```
friend_esp32_kit/
  models/
    mobilenetv3_64/model_64_int8.tflite + .h
    mobilenetv3_96/...
    mobilenetv3_128/...
    mobilenetv3_160/...
    mobilenetv3_320x240/...
    mobilenetv3_480x320/...
    custom_64/...
    custom_96/...
    custom_128/...
    custom_160/...
    custom_320x240/...
    custom_480x320/...
  esp32_benchmark/
    esp32_benchmark.ino   # flash this
  results.csv             # host results for comparison
```

## Model sizes (int8)
| Backbone | 64 | 96 | 128 | 160 | 320x240 | 480x320 |
|----------|----|----|-----|-----|---------|---------|
| MobileNetV2 0.35 | 608 KB | 608 KB | 609 KB | 609 KB | 609 KB | 609 KB |
| Custom Tiny CNN | **36 KB** | **36 KB** | 75 KB | 75 KB | 134 KB | 134 KB |

ESP32-S3 typical limits: **Flash model <800KB ideal, <400KB sweet spot. SRAM arena ~50-320KB without PSRAM, up to ~2MB with 8MB PSRAM (slower).**

Prediction: MobileNet will be tight on flash, 320x240/480x320 need PSRAM enabled else `AllocateTensors FAILED`.

## How to test each model (repeat for all 12)
1. **Arduino IDE 2.x** → Board: ESP32S3 Dev Module, Partition: 8M Flash + 16M ... + OTA, PSRAM: **Enabled** (for 320x240+), CPU: 240MHz, USB CDC On Boot: Enabled.
2. Install library: `TensorFlowLite_ESP32` (via Library Manager) or `ESP-DL` if your board uses it. If compile fails, try `esp32_benchmark` with TFLM AllOpsResolver (included).
3. Pick model, e.g. `custom_96`:
   - Copy `models/custom_96/model_96_int8.h` → `esp32_benchmark/model.h` (overwrite)
   - Edit top of `esp32_benchmark.ino`:
     ```cpp
     #define MODEL_W 96
     #define MODEL_H 96
     #define BACKBONE "custom_96"
     // and near GetModel line:
     const tflite::Model* model = tflite::GetModel(model_96_int8);
     ```
     For 320x240 use `model_320x240_int8`, 480x320 use `model_480x320_int8`.
4. Adjust arena if needed (see .ino constants). For custom 96: 120KB, for MobileNet 320x240: 1500KB etc.
5. **Flash**, open Serial 115200, copy output block starting `=== ESP32-S3 Victim Benchmark ===`.
6. If you see `AllocateTensors FAILED` → note it as **OOM** for that model, try next smaller or enable PSRAM.
7. Send back a text file with all 12 blocks.

## Expected output example
```
=== ESP32-S3 Victim Benchmark ===
Model: custom_96 96x96
Arena: 120 KB  PSRAM? yes
Arena in PSRAM
Input shape: 1 96 96 3  dtype=9
Arena used: 48320 bytes
Runs: 100  mean: 118.42 ms  min: 112 ms  max: 135 ms
Output bytes=1  first= -12
```

## What to report back
For each model: `mean ms, min, max, arena used, PSRAM yes/no, success or OOM`. Paste into `device_results.txt`.

## Host comparison (already measured on PC)
See `results.csv` — host int8 mean: mobilenet 96 ~0.7ms, custom 96 ~0.85ms (host CPU XNNPACK, ~100× faster than ESP32). ESP32 will be ~80-150ms for 96, ~250-400ms for 160, potentially >800ms for 480x320 if it boots.

Questions: ping owner if header names mismatch (`model_96_int8` vs `model_320x240_int8`).
