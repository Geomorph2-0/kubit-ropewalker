# Port `bench/` to ESP-IDF conventions (A1 + A2 + A4)

## Context

The ESP-IDF project under `bench/` does not build its own benchmark. `GUIDE.md`
§4B presents ESP-IDF as an equal alternative to the Arduino sketch, but:

- `bench/main/CMakeLists.txt` compiles only `bench.c`, whose `app_main()` is empty.
  `main.cpp` is not in the build; `bench/build/` artifacts are from the empty stub.
- `bench/main/main.cpp` is still the raw Arduino sketch: `<TensorFlowLite_ESP32.h>`,
  `Serial`, `psramFound()`, `ps_malloc()`, `delay()`, and `tflite::AllOpsResolver`
  (which the vendored `esp-tflite-micro` 1.4.0 no longer ships).
- `#include "model.h"` where `bench/main/model.h` is 0 bytes.

**Target board (confirmed): Freenove ESP32-S3 WROOM `N16R8`** — 16 MB flash, 8 MB
Octal PSRAM. Console on UART0; use the port silkscreened **UART** (CP2102 bridge),
not the native-USB port. `bench/sdkconfig` already has `FLASHSIZE_16MB`,
`SPIRAM=y`, `SPIRAM_MODE_OCT=y`, `SPIRAM_USE_MALLOC=y`, target `esp32s3`.

## Scope for this pass

Do **A1, A2, A4** only. Keep manual per-model selection (edit a small config block
+ one `#include` line in `main.cpp`) — the user wants to flash each of the 12
models by hand first and compare. The single-`#define` selector ladder (former
"A3") and `sdkconfig.defaults` / `GUIDE.md` rewrites are **deferred** to a later
pass once all 12 are confirmed.

`idf.py` is invoked after running the shell shortcut **`esp_idf`** (activates the
IDF venv). The user runs all build/flash steps themselves — see the guide at the
end.

**Goal behind the manual phase:** the user will cross-compare three sets of
per-model numbers — (1) these manual ESP-IDF runs, (2) later automatic ESP-IDF
runs via the model-selector, and (3) the Arduino sketch runs — to measure the
difference between the two toolchains/stacks (ESP-IDF vs Arduino core) on the same
board and models. So the ESP-IDF output block must stay format-compatible with the
sketch's, and build settings that affect latency (`-O2`, PSRAM 80 MHz) must be
matched deliberately, not left to defaults.

---

## A1 — `bench/main/CMakeLists.txt`

Replace the whole file with:
```cmake
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")
```
`esp-tflite-micro` is auto-added as a requirement of `main` via
`bench/main/idf_component.yml` (same as the vendored `person_detection` example,
whose `main/CMakeLists.txt` also omits it from `REQUIRES`). `INCLUDE_DIRS "."`
plus the quoted `#include` lets `main.cpp` pull headers from
`bench/main/models/<variant>/…` (all 12 confirmed present, each defines
`model_<size>_int8[]` + `_len`).

## A2 — delete `bench/main/bench.c` and `bench/main/model.h`

`bench.c` is the `idf.py create-project` stub being replaced (not user content).
`model.h` is the 0-byte placeholder; `main.cpp` will include the real header
directly from `models/…` instead.

## A4 — rewrite `bench/main/main.cpp` as an IDF app

Preserve the sketch's benchmark logic and the exact output block
(`=== ESP32-S3 Victim Benchmark ===` … `Output bytes=… first=…`). Swap the
Arduino surface for IDF:

| Arduino | IDF |
|---|---|
| `<TensorFlowLite_ESP32.h>`, `all_ops_resolver.h` | `tensorflow/lite/micro/micro_interpreter.h`, `…/micro_mutable_op_resolver.h`, `…/micro_log.h`, `tensorflow/lite/schema/schema_generated.h` |
| `#include "model.h"` | `#include "models/<variant>/model_<size>_int8.h"` (in the edit block) |
| `Serial.begin/print*` | `printf` (console = UART0 @115200) |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `psramFound()` / "Arena in PSRAM" | `esp_ptr_external_ram(tensor_arena)` — `esp_memory_utils.h` |
| `ps_malloc` / `malloc` | `heap_caps_malloc_prefer(sz, 2, MALLOC_CAP_SPIRAM\|MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL\|MALLOC_CAP_8BIT)` |
| `MicroErrorReporter` ctor arg | removed — 1.4.0 ctor: `MicroInterpreter(model, resolver, arena, size)` (verified `micro_interpreter.h:56`) |
| `tflite::AllOpsResolver` | `static tflite::MicroMutableOpResolver<7> resolver;` + `AddAdd, AddConv2D, AddDepthwiseConv2D, AddFullyConnected, AddLogistic, AddMaxPool2D, AddMean` |
| `setup()` / `loop()` | `bench_task()` + `extern "C" void app_main()` |

The 7 ops = exact union of builtin ops across all 12 shipped `.tflite` files
(extracted by parsing each flatbuffer's `operator_codes` table: custom Tiny CNN =
CONV_2D/MAX_POOL_2D/MEAN/FULLY_CONNECTED/LOGISTIC; MobileNetV2 0.35 = those minus
MAX_POOL_2D, plus ADD/DEPTHWISE_CONV_2D). Method names verified in
`micro_mutable_op_resolver.h`. Add a comment saying so — re-check if models are
regenerated.

Required specifics:
- **Edit block at top of file**, clearly fenced, holding exactly:
  `#include "models/custom_96/model_96_int8.h"`, `#define BENCH_MODEL_SYM model_96_int8`,
  `#define BENCH_NAME "custom_96"`, `#define BENCH_W 96`, `#define BENCH_H 96`.
  A comment lists the size-token → symbol map. **Gotcha:** for the rectangular
  sizes the header *filename* uses `x` but the C array *symbol* uses `_` —
  `model_320x240_int8.h` defines `model_320_240_int8`; `model_480x320_int8.h`
  defines `model_480_320_int8`. The square sizes match
  (`model_64_int8` … `model_160_int8`).
- **Run in a FreeRTOS task with an explicit 8 KB stack**
  (`xTaskCreate(bench_task, "bench", 8192, nullptr, 5, nullptr)` in `app_main`,
  then `return`). `CONFIG_ESP_MAIN_TASK_STACK_SIZE` is only 3584 here;
  `AllocateTensors()` on the 320x240 / 480x320 graphs would overflow it.
- **No `%f` in `printf`.** Compute mean/min/max from integer microseconds and print
  as `"%lld.%02lld ms"` via integer math (`whole = us/1000; frac = (us%1000)/10`).
  Robust under newlib-nano regardless of `sdkconfig`.
- Arena ladder — same thresholds as the sketch: `≤64² → 60 KB`, `≤96² → 120 KB`,
  `≤128² → 200 KB`, `≤160² → 320 KB`, `≤320×240 → 1500 KB`, else `2600 KB`
  (keyed on `BENCH_W * BENCH_H`).
- `memset(input->data.int8, 0, input->bytes)`; `WARMUP=10`, `RUNS=100`;
  keep "AllocateTensors FAILED — record as OOM" and "Arena alloc FAILED" messages.
- Print: model name + `WxH`, arena KB + `PSRAM`/`SRAM`, input shape + dtype,
  `arena_used_bytes()`, `Runs/mean/min/max`, `Output bytes=… first=…`.

---

## What is NOT changed this pass

- `bench/sdkconfig`, `bench/sdkconfig.old`, `bench/build/` — left as-is (not ours;
  not needed to make the port build). The `-Og` default optimization and 40 MHz
  PSRAM speed are called out in the guide as menuconfig steps the user chooses
  before trusting the numbers.
- `bench/main/models/` — now load-bearing (included by `main.cpp`); keep in sync
  with the top-level `models/`.
- `GUIDE.md` — untouched this pass (§4B rewrite deferred).

---

## Deliverables

1. This plan copied to **`/home/rbj/Downloads/friend_esp32_kit_v2/BENCH_IDF_PORT_PLAN.md`**.
2. A1 + A2 + A4 applied.
3. Step-by-step build/flash guide (below) in the chat response.

---

## Build / flash guide (user runs these)

```bash
esp_idf                              # activate IDF venv (provides idf.py)
cd ~/Downloads/friend_esp32_kit_v2/bench

# bench/sdkconfig already targets esp32s3 (16 MB flash, Octal PSRAM) — do NOT run
# `idf.py set-target` (it resets sdkconfig to defaults). If the old CMake cache
# fights you:  idf.py fullclean   (keeps sdkconfig, drops build/)

# One-time build alignment (already done via menuconfig — verify it stays):
#   Compiler options        → Optimization Level     → Optimize for performance (-O2)
#   Component config → ESP PSRAM → SPI RAM config     → RAM clock speed → 80 MHz
#   Component config → ESP System Settings           → CPU frequency   → 240 MHz
# The firmware now PRINTS what it was compiled with, so you don't have to track it:
#   opt=O2  psram=80MHz  cpu=240MHz   <- expected fingerprint

# per model: edit the fenced block at the top of main/main.cpp
#   (#include line + BENCH_MODEL_SYM / BENCH_NAME / BENCH_W / BENCH_H)
idf.py build

# ---- recording ----
# The firmware prints to SERIAL ONLY. results/device_results.txt is written by the
# host `| tee -a` below — NOT by the device. Run `idf.py monitor` without the pipe
# and nothing is saved. (`ls /dev/ttyUSB* /dev/ttyACM*` to find the CP2102 port.)
idf.py -p /dev/ttyUSB0 flash monitor | tee -a ../results/device_results.txt
#   monitor auto-resets the chip -> one self-labelled block appends to the log.
#   Ctrl-] to exit monitor, edit the model block, re-run this line, repeat.
#   Forgot the pipe? Inside monitor: Ctrl+T Ctrl+L toggles logging to a file.

# expect within ~3 s (the ### and CSV lines are new, auto-generated):
#   ### custom_96 96x96  stack=idf  opt=O2  psram=80MHz  cpu=240MHz  note=
#   === ESP32-S3 Victim Benchmark ===
#   Model: custom_96 96x96
#   Arena cap: 2048 KB
#   Arena in PSRAM
#   Input shape: 1 96 96 3  dtype=9
#   Arena used: NNNNN bytes
#   Fits internal SRAM (~320 KB): yes/no
#   Runs: 100  mean: NN.NN ms  min: NN.NN ms  max: NN.NN ms
#   Output bytes=1  first=NN
#   CSV,idf,custom_96,96,96,OK,PSRAM,NN.NN,NN.NN,NN.NN,NNNNN,-12,O2,80,240,

# after all runs — extract the machine-readable table (grep > overwrites safely,
# it re-scans the whole append-only log each time):
grep '^CSV,' ../results/device_results.txt > ../results/device_csv.txt
```

### CSV columns
```
CSV,stack,model,W,H,status,arena_loc,mean_ms,min_ms,max_ms,arena_used_bytes,first_out,opt,psram_mhz,cpu_mhz,note
```
- `status` ∈ `OK | ARENA_ALLOC_FAIL | SCHEMA_MISMATCH | ALLOCATE_TENSORS_FAIL | INVOKE_FAIL`
  — the OOM cases still emit a CSV line (ms fields `0.00`), so failures are data too.
- Same format from the Arduino sketch (`stack=arduino`, `note=esptflm:<batch>`,
  `psram_mhz=80` — the Arduino core also clocks OPI PSRAM at 80 MHz); the two merge
  directly. `note` is set by `#define BENCH_NOTE "..."` to tag a batch.
- Multiple runs of one model just append; when aggregating, keep the **last**
  `CSV,` line per `(stack,model,W,H,note)`.

### Arena sizing

The kit's `results/results.csv` `arena_est_kb` column is a static estimate and
runs **~2.5× low** (`custom_64`: 34 KB estimated, ~85 KB actual on-device). Both
firmwares now allocate a deliberately oversized PSRAM arena (2 MB for ≤160², 4 MB
for 320×240, 6 MB for 480×320) — this does **not** affect `Invoke()` latency or
`arena_used_bytes()`. The `Arena used:` line / CSV `arena_used_bytes` (field 11) is
the **authoritative** per-model memory figure and supersedes `arena_est_kb`.
Override for a single stubborn model with an inline `#define BENCH_ARENA_KB 7168`
above the model include.

Troubleshooting:
- `AllocateTensors FAILED` should no longer happen with the oversized arena. If it
  does on `mobilenetv3_480x320`, bump `#define BENCH_ARENA_KB` toward 7168 (PSRAM
  pool ≈ 8 MB minus ~300 KB in use). The int8 `MEAN` kernel is the likely limiter.
- Blank monitor → wrong USB port (use **UART** / CP2102, not native USB), or wrong
  `-p` device.
- Toolchain: the boot log shows **ESP-IDF v6.1** (`~/.espressif/v6.1/esp-idf`,
  activated by the `esp_idf` shortcut), which matches `dependencies.lock`. Any
  "5.5.3" refers only to an unused PlatformIO copy of the framework.

### Known issues

- **Task-watchdog warning during the run — FIXED 2026-08-31.** The first
  `custom_64` OK run printed `E (NNNN) task_wdt: ... IDLE0 (CPU 0) did not reset in
  time` (backtrace through `esp_nn_conv_s8_esp32s3` / `MicroInterpreter::Invoke` /
  `bench_task`). Cause: `bench/sdkconfig` has `CONFIG_FREERTOS_HZ=100` (10 ms
  tick), so the inter-invoke `vTaskDelay(pdMS_TO_TICKS(2))` rounded to
  `vTaskDelay(0)` — not a real yield — and `IDLE0` starved through the 100-run
  loop. Warn-only (no `CONFIG_ESP_TASK_WDT_PANIC`), so `mean`/`min` stayed valid
  but `max` was inflated by the WDT ISR + backtrace print. Fixed:
  `vTaskDelay(pdMS_TO_TICKS(2))` → `vTaskDelay(1)` at both delay sites in
  `bench/main/main.cpp` (one full tick = guaranteed yield). Any run logged
  *before* this fix — the `custom_64` run 2 in `results/device_results.txt`
  (mean 85.85 / min 85.51 / **max 118.62**) — has an unreliable `max`; re-run
  supersedes it. The Arduino sketch was never affected (1000 Hz tick).

---

# Arduino cross-stack sweep  (added 2026-09-01)

Second half of the stack comparison: run the same 12 models through the **Arduino
core** instead of bare ESP-IDF.

## Library — `esptflm` only (one library, 12 flashes)

The arduino-esp32 **3.x core bundles** the esp-tflite-micro + esp-nn archives
(`libespressif__esp-tflite-micro.a` 1.3.5, `libespressif__esp-nn.a` 1.1.2 in core
3.3.7). No library to install — the "TFLite Micro" Library-Manager entry is just an
example stub. `sketches/esp32_benchmark/esp32_benchmark.ino` is the Arduino
counterpart of `bench/main/main.cpp`: same 16-col CSV, same `###` header, same
arena ladder, new 4-arg `MicroInterpreter` API, `MicroMutableOpResolver<7>` with
the identical op set.

**Dropped:** `TensorFlowLite_ESP32` (tanakamasayuki), the library the kit's
`GUIDE.md` line ~72 tells people to install. It is abandoned at v1.0.0 (2021) and
**does not compile** against core 3.x — `constexpr` error in its vendored
flatbuffers (`stl_emulation.h:386`), not fixable with `-fpermissive`. GUIDE.md
should be corrected. The current sketch does not reference it, so it is harmless if
still installed.

## Port & Tools menu

**Plug into the Type-C port next to the CP2102 chip** (silkscreen **UART**) →
`/dev/ttyUSB0` (cp210x driver; `ls /dev/ttyUSB* /dev/ttyACM*` to confirm).

| Tools ▸ … | value | why |
|---|---|---|
| Board | ESP32S3 Dev Module (or "Freenove ESP32-S3") | |
| CPU Frequency | 240 MHz | match IDF |
| Flash Size | 16 MB | |
| PSRAM | **OPI PSRAM** | core builds it at 80 MHz OCT = same as IDF |
| **USB CDC On Boot** | **Disabled** | → `Serial` = UART0 = the CP2102 port. That link **survives resets** (native USB-CDC drops off the bus every reset / esptool op) and it's the **same console path the IDF runs use** — one fewer variable. |
| USB Mode | Hardware CDC and JTAG (default) | |
| Partition Scheme | Huge APP (any ≥ 3 MB app) | |

*(Native-USB alternative: plug the other Type-C port, set CDC On Boot = **Enabled**,
use `/dev/ttyACM0` and `CDCOnBoot=cdc` in the FQBN — then RESET-tap whenever the
monitor desyncs.)*

## Running it — one model at a time

Strictly sequential, ONE shell. **Only one process can hold the serial port** — run
`monitor` only *after* `upload` finishes, or `upload` fails *"port is busy"*.
```bash
cd ~/Downloads/friend_esp32_kit_v2/sketches/esp32_benchmark
FQBN='esp32:esp32:esp32s3:PSRAM=opi,CPUFreq=240,FlashSize=16M,CDCOnBoot=default,PartitionScheme=huge_app'
./select_model.sh custom 64                        # rewrites the 5 #defines, no drift
arduino-cli compile -b "$FQBN" -u -p /dev/ttyUSB0 .   # compile + upload; board reboots
arduino-cli monitor  -p /dev/ttyUSB0 -c baudrate=115200 | tee -a ../../results/device_results.txt
#   wait for the  ### … / CSV,arduino,…  block, then Ctrl-C.  Missing? tap RESET.
```
Repeat for each of the 12 models. Arduino IDE equivalent: `select_model.sh`,
**Sketch ▸ Upload**, **Serial Monitor @ 115200**, paste the block.

All 12 model headers compile-checked (449 KB custom … 1035 KB mobilenetv3, all fit
the 3 MB app partition). First boot line must read `psram=80MHz cpu=240MHz`.

## Expected / confounds

- 12 new `CSV,arduino,<model>,…,Os,80,240,esptflm:manual` lines. `opt=Os` (Arduino
  core has no Optimize menu for esp32s3 — sketch built `-Os`) vs the IDF rows'
  `opt=O2`. This labels only the sketch TU; the esp-nn kernels doing the work are
  Espressif-precompiled in **both** stacks, so it is not the dominant term.
- `esptflm` vs `idf` therefore measures: esp-tflite-micro 1.3.5/esp-nn 1.1.2
  (precompiled) + Arduino core startup & dual-core scheduling  **vs**  1.4.0/1.3.1
  (source `-O2`) + bare `app_main`. Treat the *set* of rows as the comparison, not
  any single delta. PSRAM 80 MHz / CPU 240 MHz / OCT mode are matched.
- Arduino core tick = 1 kHz, so the inter-invoke `delay(2)` really yields — no
  task-WDT issue (unlike the IDF side pre-fix). Expect `min ≈ mean`.
- Checked in core 3.3.7 `qio_opi/sdkconfig.h`: `NEWLIB_NANO_FORMAT` **not set**
  (so the `%.2f` CSV fields print fine), `CONFIG_PM_ENABLE` **not set** (no DFS —
  CPU holds 240 MHz between invokes). Neither stack downclocks.
- 480×320 arena tier is 5 MB on Arduino (vs 6 MB on IDF) — a 6 MB `ps_malloc`
  risks failing under the core's PSRAM reserve; actual `arena_used` is 3.08 MB so
  5 MB is ample. A failure still emits a clean `ARENA_ALLOC_FAIL` row.

## Results so far — IDF manual sweep (all 12, clean, post-WDT-fix)

| backbone | 64 | 96 | 128 | 160 | 320×240 | 480×320 |
|---|--:|--:|--:|--:|--:|--:|
| custom (Tiny CNN) | 84 | 187 | 447 | 695 | 2363 | 4726 |
| mobilenetv3 (MobileNetV2 α0.35) | 102 | 170 | 274 | 434 | 1284 | 2645 |

*mean ms, `stack=idf opt=O2 psram=80 cpu=240`.* Crossover near 96²: Tiny CNN wins
at 64², parity at 96², MobileNetV2 pulls ~1.8× ahead by 480×320. `arena_used`
(CSV field 11) ranges 85 KB (custom_64) → 3.08 MB (custom_480x320); only
custom_64/96 and mobilenetv3_64/96 fit internal SRAM.

## Follow-ups (still deferred)

- `results/collect_device_csv.py` — merge all `CSV,` lines, join idf/arduino/kit
  static `results/results.csv`, report deltas.
- `#define BENCH_MODEL_ID 1..12` selector ladder in `bench/main/main.cpp` for
  automatic IDF runs (now unblocked — 12 models confirmed).

---

## Verification done statically (cannot compile here — no `idf.py` on PATH)

- `MicroInterpreter(const Model*, const MicroOpResolver&, uint8_t*, size_t, …)` —
  `micro_interpreter.h:56`.
- `AddAdd/AddConv2D/AddDepthwiseConv2D/AddFullyConnected/AddLogistic/AddMaxPool2D/AddMean`
  present — `micro_mutable_op_resolver.h`.
- No `all_ops_resolver.h` in the vendored component (confirms the port is needed).
- Op union — parsed from all 12 `.tflite` `operator_codes` tables.
- `esp_ptr_external_ram` `esp_memory_utils.h:316`; `heap_caps_malloc_prefer`
  `esp_heap_caps.h:385`; `MALLOC_CAP_SPIRAM` `esp_heap_caps.h:39`.
- `person_detection/main/CMakeLists.txt` omits `esp-tflite-micro` from `REQUIRES`
  (managed dep auto-required) — pattern for A1.
