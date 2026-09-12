# Implementation Guide — Victim Detection on ESP32-S3 (MobileNet 96 default, 128 optional)

Use **one** of two models to detect a victim and act. Default is `mobilenetv3_96` with threshold `0.65`.

## 1. What's in this pack

```
friend_esp32_pack_mnet96/
  IMPLEMENTATION_GUIDE.md   <- this file
  QUICKSTART.md             <- 1-page version
  README.md                 <- what to send back
  sketches/esp32_camera_victim/
    esp32_camera_victim.ino <- inference loop (camera -> preprocess -> Invoke -> action)
    config_model.h          <- EDIT THIS: model size, threshold, board (default 96 / 0.65)
    model.h                 <- YOU create: copy of model_96_int8.h (or model_128_int8.h)
  models/
    mobilenetv3_96/         <- model_96_int8.h + .tflite + tflite_meta.json + metrics.json
    mobilenetv3_128/        <- model_128_int8.h + .tflite + meta + metrics
  results/
    DEVICE_BENCHMARK_REPORT.md  <- on-device timings for all 12 (your models are 2 rows)
    BENCHMARK_REPORT.md         <- host + training background
    results.csv                 <- full host table (96/128 are the rows you need)
```

**No `.h5`, no Python, no training.** Those stay with the owner.

## 2. Model choice (why these two)

From `results/DEVICE_BENCHMARK_REPORT.md` (Freenove N16R8, 240MHz, PSRAM 80MHz OCT):

| Model | Int8 flash | Arena used (IDF / Arduino) | On-device mean (IDF / Arduino) | Test acc (prec / rec) |
|-------|-----------|---------------------------|-------------------------------|----------------------|
| **mobilenetv3_96 (DEFAULT)** | 608KB | 219KB / 318KB | 170ms / 178ms | **87.7%** (0.84 / 0.875) |
| mobilenetv3_128 (optional) | 609KB | 315KB / 510KB | 274ms / 283ms | 87.7% (0.87 / 0.833) |

- **96** is the default: faster, smaller arena, same accuracy as 128.
- **128** if you want slightly better detail on victim cues and can afford ~100ms extra + PSRAM.
- Both need an 8MB-flash partition with room for a ~608KB model; both need PSRAM enabled (128 strictly, 96 strongly recommended).

## 3. Hardware (same convention as the benchmark kit)

Works on either — you decide:

- **DevKitC-1 N8R8** (breadboard, 8MB Flash + 8MB PSRAM Octal) + OV2640 breakout, or
- **XIAO ESP32S3 Sense** (thumb-size, same 8MB/8MB, OV2640 included)

Requirement: **S3 R8 variant with PSRAM**. Check silkscreen `S3R8`. Set `CAM_BOARD` in `config_model.h`: `0` = XIAO, `1` = DevKitC-1 breakout (AI-Thinker style pins — adjust if your wiring differs).

## 4. Software setup (same convention as the benchmark kit)

**A) Arduino IDE 2.x (recommended):**
1. Boards Manager -> `esp32` by Espressif **3.0.5+**.
2. Library Manager -> **TensorFlowLite_ESP32**.
3. Board: `ESP32S3 Dev Module` (DevKitC-1) or `XIAO_ESP32S3`.
4. Settings:
   ```
   Flash Size: 8MB (or 16MB if yours)
   Partition Scheme: 8M with spiffs + OTA
   PSRAM: Enabled (OPI PSRAM)   <- REQUIRED
   CPU Frequency: 240MHz
   USB CDC On Boot: Enabled
   ```
5. Driver if no port: `CP210x` (DevKit) or `CH343` (XIAO). Use a data USB cable.

**B) ESP-IDF 5.3:** `idf.py add-dependency esp-tflite-micro`, copy `model.h` into `main/`, adapt the `.ino` to `main.cpp`, enable Octal PSRAM in menuconfig, `idf.py flash monitor`. If you use Arduino, ignore this.

## 5. Workflow: preprocessing -> inference (exact training match)

Training used (`dataset/process_data.py`, `dataset/train.py`):

1. **Letterbox** — photo is aspect-fit resized, pasted centered on a **black** `W x H` canvas. Never stretched. The sketch `letterbox_rgb565_to_rgb888()` does the same with nearest-neighbor (aspect + centering + black pad are what matter).
2. **MobileNet preprocess** — each RGB byte `p` becomes `p / 127.5 - 1.0` (range -1..1). **Do not feed 0-255 raw** (that rule is for the old custom backbone, not used here).
3. **Quantize** — float is converted to int8 with the model's own `scale / zero_point`:
   `q = round(prep / scale) + zp`, clamped to -128..127.
4. **Invoke** — single `interpreter->Invoke()`, timed with `esp_timer_get_time()`.
5. **Dequant output** — `score = (out_int8 - zp_out) * scale_out`, clamped 0..1. This is the victim probability (sigmoid).
6. **Decide + act** — `score > THRESHOLD` (default **0.65**) => victim.

## 6. Flash and run (default 96 / 0.65)

```
sketches/esp32_camera_victim/
  esp32_camera_victim.ino
  config_model.h   <- already default: 96, "mobilenetv3_96", 0.65
  model.h          <- YOU CREATE THIS FILE
```

1. Copy the default header in:
   ```powershell
   copy models\mobilenetv3_96\model_96_int8.h sketches\esp32_camera_victim\model.h
   ```
2. Confirm `config_model.h` (defaults — no edit needed):
   ```cpp
   #define MODEL_W 96
   #define MODEL_H 96
   #define BACKBONE "mobilenetv3_96"
   #define THRESHOLD 0.65f
   #define CAM_BOARD 0   // 0 = XIAO, 1 = DevKitC-1
   ```
3. Arduino: open `esp32_camera_victim.ino`, Upload, Serial Monitor **115200**.
4. Expected boot log:
   ```
   === ESP32-S3 Victim Inference ===
   Model: mobilenetv3_96 96x96  THRESHOLD=0.65
   PSRAM: yes  Arena: 384 KB
   Arena used: ~219000 bytes
   Input: [1 96 96 3] type=... scale=... zp=...
   SD_MMC ready: /victim_XXXX.jpg
   Ready. Looping capture -> infer ...
   score=0.821 VICTIM (172.3 ms)
   ```
5. Point the camera at a printed victim / healthy image. `VICTIM` lines + `/victim_N.txt` markers on SD mean the pipeline works.

### Switch to 128 (optional)

```powershell
copy models\mobilenetv3_128\model_128_int8.h sketches\esp32_camera_victim\model.h
```
Then in `config_model.h` comment the 96 block, uncomment the 128 block. Re-flash. Expect ~280ms per inference and `Arena used ~315000-510000` bytes.

### Full-resolution victim picture (UXGA upgrade)

The sketch saves a `/victim_N.txt` marker per detection (score + model + threshold) so the loop keeps up. For a real HQ JPEG: in `loop()`'s victim branch, deinit the RGB565 QVGA stream and reinit at `FRAMESIZE_UXGA` + `PIXFORMAT_JPEG`, `esp_camera_fb_get()`, save `fb->buf` to `/victim_N.jpg`, then reinit back to QVGA RGB565. This costs ~300-500ms per save — do it only on victim, not every frame.

## 7. Threshold tuning (default 0.65)

From `models/mobilenetv3_96/metrics.json` (test @0.5: prec 0.84, rec 0.875):

| THRESHOLD | Effect | When to use |
|-----------|--------|-------------|
| 0.50 | max recall — catches more victims, more false alarms | track test with tricky healthy prints |
| **0.65 (default)** | balanced toward precision — fewer false alarms | competition default |
| 0.70 | max precision — only confident victims | noisy lighting, many healthy decoys |

For 128 (`metrics.json` prec 0.87 / rec 0.833): same 0.65 default works; drop to 0.55 if it misses victims.

Procedure: hold 5 victim + 5 healthy prints in front of the camera, log 10 scores, pick the threshold that separates them.

## 8. Troubleshooting

| Problem | Fix |
|---------|-----|
| `Arena alloc FAILED!` | `Tools > PSRAM: Enabled (OPI)`. Board must be R8 (check `PSRAM: yes` line). |
| `AllocateTensors FAILED!` | Arena too small — `config_model.h` already sizes 384KB (96) / 640KB (128) with headroom. If edited down, restore. |
| `Camera init FAILED 0x...` | Wrong `CAM_BOARD` or wiring; confirm PSRAM enabled (camera needs it). XIAO = 0, DevKit breakout = 1. |
| `Model schema mismatch!` | `model.h` doesn't match `config_model.h` (e.g. 128 header with 96 config). Re-copy the right header. |
| All scores ~0.5 / garbage | Preprocess bug — confirm you did NOT skip the `/127.5 - 1` step. Raw 0-255 into a mobilenet model gives nonsense. |
| `Guru Meditation` on Invoke | Reinstall `TensorFlowLite_ESP32`, clean build. Keep `vTaskDelay(1)` in loop (WDT fix from device benchmark). |
| SD not found | Continues fine without SD — scores still print. Check wiring / format FAT32 if you need saves. |
| Slow / watchdog resets | Increase `LOOP_DELAY_MS`, keep `vTaskDelay(1)`, stay at 96 unless you need 128. |

## 9. Test checklist (send results back)

- [ ] 96 default flashes, boot log shows `Arena used ~219000` and `Input: [1 96 96 3]`
- [ ] Healthy print -> `score=0.1-0.4 healthy`
- [ ] Victim print -> `score=0.7-0.95 VICTIM`
- [ ] Vary light / angle / distance — note any flip
- [ ] (Optional) repeat with 128, note `Arena used` + mean ms
- [ ] Send back: Serial log excerpt + threshold you settled on + which model you ship

## 10a. ESP-IDF variant (Freenove ESP32-S3 WROOM, OV3660)

For users building with ESP-IDF directly (not Arduino), a second, independent
firmware lives at `sketches/esp32_camera_victim_idf/`. It implements the exact
same pipeline (letterbox -> mobilenet preprocess -> int8 quantize -> Invoke ->
dequant -> threshold -> SD marker save) but targets a third board not covered
by the Arduino sketch above: the **Freenove ESP32-S3 WROOM (N16R8)** with an
**OV3660** camera sensor — the same board used for
`results/DEVICE_BENCHMARK_REPORT.md`. It does not support XIAO/DevKitC-1;
camera and SD pins are fixed for Freenove.

Build (assumes an `esp-idf` shell alias that activates the `idf.py` venv):
```
esp-idf
cd sketches/esp32_camera_victim_idf
idf.py set-target esp32s3   # regenerates sdkconfig from sdkconfig.defaults
idf.py build
idf.py -p <PORT> flash monitor
```

Model/threshold are configured the same `#define` way, in
`sketches/esp32_camera_victim_idf/main/config_model.h` (`MODEL_W/H`,
`BACKBONE`, `THRESHOLD`). Camera pins there match Espressif's
`CAMERA_MODEL_ESP32S3_EYE` layout (confirmed against Freenove's own
`Sketch_07.3_Camera_SDcard` example for this board). SD card pins are fixed
at CLK=39, CMD=38, D0=40 (1-bit SDMMC, per Freenove's own board reference) —
SD mount failure is non-fatal, matching the Arduino sketch's behavior.

Expected boot log:
```
=== ESP32-S3 Victim Inference ===
Model: mobilenetv3_96 96x96  THRESHOLD=0.65
PSRAM: yes  Arena: 384 KB
Arena used: <N> bytes
Input: [1 96 96 3] type=<kTfLiteInt8> scale=<f> zp=<z>
Camera init OK
SD mount OK   (or: SD mount FAILED — continuing without SD save)
Ready. Looping capture -> infer ...
score=0.xxx healthy (xx.x ms)
score=0.7xx VICTIM (xx.x ms)
Saved /victim_1.txt
```
If `AllocateTensors()` instead logs `Didn't find op for builtin opcode 'X'`,
add the matching `resolver.AddX()` call in `main.cpp` — the shipped 7-op
resolver (Add, Conv2D, DepthwiseConv2D, FullyConnected, Logistic, MaxPool2D,
Mean) is proven against these exact model files but should be re-checked if
the models are ever regenerated.

## 10. File reference

- Sketch: `sketches/esp32_camera_victim/esp32_camera_victim.ino` (`letterbox_*`, `preprocess_mobilenet`, quant loop, `Invoke`, dequant, SD stub)
- Config: `sketches/esp32_camera_victim/config_model.h` (MODEL_W/H, THRESHOLD, CAM_BOARD, arena)
- Models: `models/mobilenetv3_96/` and `models/mobilenetv3_128/` (`.h` + `.tflite` + `tflite_meta.json` + `metrics.json`)
- Numbers: `results/DEVICE_BENCHMARK_REPORT.md`, `results/results.csv`, `results/BENCHMARK_REPORT.md`
