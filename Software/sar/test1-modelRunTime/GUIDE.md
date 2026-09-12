# Friend Guide — Victim vs NotVictim ESP32-S3 Benchmark (All 12 Models)

You received **12 int8 models** = 2 backbones × 6 resolutions. Your job is **benchmark only**: flash each, time `interpreter->Invoke()`, report back. No Python, no training.

## 1. What’s in this zip

```
friend_esp32_kit_v2/
  GUIDE.md                          ← this file
  results/
    BENCHMARK_REPORT.md              host results + plots (your target to reproduce on-device)
    results.csv / results.json       host table: accuracy / size / host latency
    plot_*.png                       4 plots
  models/
    mobilenetv3_64/model_64_int8.h/.tflite    608KB  (MobileNetV2 α=0.35, ImageNet)
    mobilenetv3_96/model_96_int8.h/.tflite    608KB  val86.0 test87.7% **recommended**
    mobilenetv3_128/...                       609KB
    mobilenetv3_160/...                       609KB
    mobilenetv3_320x240/...                   609KB  needs PSRAM
    mobilenetv3_480x320/...                   609KB  needs PSRAM
    custom_64/model_64_int8.h/.tflite         36KB   (Tiny CNN 27k params)
    custom_96/...                              36KB  **recommended if flash tight**
    custom_128/...                             75KB
    custom_160/...                             75KB
    custom_320x240/...                        134KB  needs PSRAM
    custom_480x320/...                        134KB  needs PSRAM
    (each also has tflite_meta.json + metrics.json)
  sketches/esp32_benchmark/
    esp32_benchmark.ino              benchmark sketch (no camera)
```

**No `.h5` in this zip** — those are training artifacts, not needed on ESP32. If you need them, ask for `friend_esp32_kit_full.zip`.

## 2. Host reference (already measured on my PC)

From `results/results.csv` — int8, 80 runs, XNNPACK host CPU (ESP32 will be ~80-150× slower but ranking holds):

| Backbone | Size | Int8 KB | ArenaEst KB | Test Acc | Host int8 mean |
|----------|------|---------|-------------|----------|----------------|
| mobilenetv3 | 64 | 608 | 48 | 75.4% | 0.12 ms |
| mobilenetv3 | **96** | 608 | 108 | **87.7%** | 0.71 ms |
| mobilenetv3 | 128 | 609 | 192 | 87.7% | 0.58 ms |
| mobilenetv3 | 160 | 609 | 300 | 89.5% | 1.07 ms |
| mobilenetv3 | 320x240 | 609 | 900 | 91.2% | 2.49 ms |
| mobilenetv3 | 480x320 | 609 | 1800 | **94.7%** | 7.63 ms |
| custom | 64 | 36 | 34 | 70.2% | 0.13 ms |
| custom | **96** | 36 | 76 | 71.9% | 0.85 ms |
| custom | 128 | 75 | 134 | 73.7% | 0.68 ms |
| custom | 160 | 75 | 210 | 75.4% | 1.51 ms |
| custom | 320x240 | 134 | 630 | 80.7% | 3.68 ms |
| custom | 480x320 | 134 | 1260 | 77.2% | 9.04 ms |

ESP32-S3 limits: **Flash <800KB ideal** (mobilenet 608KB is borderline, custom 36KB easy), **SRAM arena ~320KB** without PSRAM, **~2MB** with 8MB PSRAM (octal). Expect **on-device 96 → ~80-150ms, 160 → ~250-400ms, 480x320 → >800ms or OOM**.

## 3. Before you flash — pick any board, any IDE

**Hardware — works on either, you decide:**
- **DevKitC-1 N8R8** (breadboard, 8MB Flash + 8MB PSRAM Octal, USB-UART + USB-OTG) or
- **XIAO ESP32S3 Sense** (thumb-size, same 8MB/8MB, OV2640 included)
- Requirement: **N8R8 / R8 variant with 8MB PSRAM** for 320x240/480x320. Check silkscreen: `S3R8`. No camera needed for benchmark (sketch uses dummy tensor).

**IDE — pick one:**
- **A) Arduino IDE 2.x** (easiest, no install script)
- **B) ESP-IDF 5.3 + esp-tflite-micro component** (if you already use IDF)

Both use the **same** `model_{size}_int8.h` + `model_{size}_int8.tflite`.

## 4. Setup

### A) Arduino IDE
1. Install **Arduino IDE 2.x** → Boards Manager → search `esp32` → install **Espressif 3.0.5+**.
2. Library Manager → install **TensorFlowLite_ESP32** (or `esp-tflite-micro` if listed) and **ESP32 Camera** not needed for this sketch but harmless.
3. Select board:
   - For DevKitC-1: `Tools > Board > ESP32S3 Dev Module`
   - For XIAO Sense: `Tools > Board > XIAO_ESP32S3` (add Seeed JSON if missing)
4. Set:
   ```
   Tools > Flash Size: 8MB (or 16MB if yours)
   Tools > Partition Scheme: 8M with spiffs + OTA  (or Default 8M)
   Tools > PSRAM: Enabled  (OPI PSRAM)  — REQUIRED for 320x240/480x320, harmless for 96
   Tools > CPU Frequency: 240MHz (WiFi)
   Tools > USB CDC On Boot: Enabled
   Tools > USB Mode: Hardware CDC and JTAG
   ```
5. Install driver if port not showing: `CP210x` (DevKit) or `CH343` (XIAO).

### B) ESP-IDF
```bash
idf.py create-project bench && cd bench
idf.py add-dependency esp-tflite-micro
# copy model.h into main/ and esp32_benchmark.ino content into main/main.cpp (small adapt)
idf.py menuconfig  # → Component config → ESP PSRAM → Support for Octal PSRAM → Enable
idf.py -p COMx flash monitor
```
If you stay with Arduino, ignore this.

## 5. Flash & benchmark each of the 12 (repeat)

For **each** `models/<backbone>_<size>/` do:

**Step 1 — Put the right header in the sketch**
```
sketches/esp32_benchmark/
  esp32_benchmark.ino
  model.h   ← you will overwrite this each time
```
Copy, e.g. for `custom_96`:
```powershell
copy models\custom_96\model_96_int8.h sketches\esp32_benchmark\model.h
```
For rectangular `models/custom_320x240/model_320x240_int8.h` → `model.h`, etc.

**Step 2 — Edit the 4 lines at top of `esp32_benchmark.ino`**

Open `esp32_benchmark.ino` and set to match the model you just copied:

For `custom_96`:
```cpp
#define MODEL_W 96
#define MODEL_H 96
#define BACKBONE "custom_96"
extern const unsigned char model_96_int8[];
extern const unsigned int model_96_int8_len;
const tflite::Model* model = tflite::GetModel(model_96_int8);
```

For `mobilenetv3_96`:
```cpp
#define MODEL_W 96
#define MODEL_H 96
#define BACKBONE "mobilenetv3_96"
extern const unsigned char model_96_int8[];
extern const unsigned int model_96_int8_len;
const tflite::Model* model = tflite::GetModel(model_96_int8);
```

For `custom_320x240`:
```cpp
#define MODEL_W 320
#define MODEL_H 240
#define BACKBONE "custom_320x240"
extern const unsigned char model_320x240_int8[];
extern const unsigned int model_320x240_int8_len;
const tflite::Model* model = tflite::GetModel(model_320x240_int8);
```

For `mobilenetv3_480x320`:
```cpp
#define MODEL_W 480
#define MODEL_H 320
#define BACKBONE "mobilenetv3_480x320"
extern const unsigned char model_480x320_int8[];
extern const unsigned int model_480x320_int8_len;
const tflite::Model* model = tflite::GetModel(model_480x320_int8);
```

*(The `.h` file name tells you the array name — open the `.h` and the first line `const unsigned char model_xxx_int8[]` is what you extern.)*

**Step 3 — Arena is auto-set by MODEL_W/H**
```cpp
60KB @64, 120KB @96, 200KB @128, 320KB @160, 1500KB @320x240 (PSRAM), 2600KB @480x320 (PSRAM)
```
If you see `AllocateTensors FAILED` or `Arena alloc FAILED`, that **is** a valid result — record as `OOM`.

**Step 4 — Upload**
- Arduino: `Sketch > Upload`, then `Tools > Serial Monitor 115200`
- IDF: `idf.py flash monitor`

**Step 5 — Copy Serial output**

Wait ~3s for:
```
=== ESP32-S3 Victim Benchmark ===
Model: custom_96 96x96
Arena: 120 KB  PSRAM? yes
Arena in PSRAM
Input shape: 1 96 96 3  dtype=...
Arena used: 48320 bytes
Runs: 100  mean: 118.42 ms  min: 112.00 ms  max: 135.00 ms
Output bytes=1  first= -12
```

That’s the data we need.

## 6. What to send back

Create `device_results.txt` and paste all 12 blocks, or fill this table:

```
backbone,size,W,H,result,psram,mean_ms,min_ms,max_ms,arena_used,notes
mobilenetv3,64,64,64,OK,yes, , , , ,
mobilenetv3,96,96,96,OK,yes, , , , ,
mobilenetv3,128,128,128,OK,...
mobilenetv3,160,160,160,OK,...
mobilenetv3,320x240,320,240,OOM?,...
mobilenetv3,480x320,480,320,OOM?,...
custom,64,64,64,OK,...
custom,96,96,96,OK,...
custom,128,128,128,OK,...
custom,160,160,160,OK,...
custom,320x240,320,240,OK/OOM,...
custom,480x320,480,320,OK/OOM,...
```

If `OOM`, just write `AllocateTensors FAILED` and that counts.

Also return the **Serial logs as .txt** for each (or one combined). No need to change anything else.

## 7. How we will use the numbers

Host `results.csv` already has accuracy/size/host-latency. Once we have your `mean_ms` per model, we add column `device_mean_ms`, make final `plot_accuracy_vs_latency (ESP32)` and choose deploy model:

- **If flash tight (<100KB):** `custom_96` (36KB, ~70% test) or `custom_128` (75KB, 73.7%)
- **If accuracy matters & 608KB flash OK:** `mobilenetv3_96` (87.7% test, 108KB arena, fits SRAM)
- **Native 480x320 is ablation only** — 16× RAM for +7% test; for robot we will letterbox camera to 96/128.

## 8. Troubleshooting (hardware-agnostic)

| Problem | Fix |
|---------|-----|
| `Arena alloc FAILED!` | Enable PSRAM: `Tools > PSRAM: Enabled (OPI)` or use smaller model (custom_96). Verify board is N8R8/R8 (check `psramFound()` log). |
| `AllocateTensors FAILED` | Model too large for arena — increase `kTensorArenaSize` by +200KB or enable PSRAM or drop to 96. |
| `Model schema mismatch` | Wrong `model.h` vs `GetModel` array — ensure extern and GetModel use same `model_xx_int8` as the `.h`. |
| Port not listed | Install CP210x (DevKit) or CH343 (XIAO) driver; try other USB cable (data, not charge). |
| `Guru Meditation` on Invoke | Library mismatch — reinstall `TensorFlowLite_ESP32`  (Arduino) or `esp-tflite-micro` (IDF), set `Compiler warnings: None`. |
| Slow flash / timeout | Set `Upload Speed: 921600`, hold BOOT 3s before Upload on DevKit. |

## 9. After you return results

I’ll merge into `results/device_metrics.csv`, generate updated `plot_accuracy_vs_latency_device.png`, and finalize recommendation for the crawler bot. No camera needed for this stage; camera integration is next step after benchmark pick.

Questions? Send a screenshot of Serial output if unsure — one block is enough.
