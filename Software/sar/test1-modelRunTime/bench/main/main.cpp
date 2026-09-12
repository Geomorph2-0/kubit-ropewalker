/*
  main.cpp — ESP-IDF port of the "Victim vs NotVictim" on-device latency benchmark
  Target: Freenove ESP32-S3 WROOM N16R8 (16 MB flash, 8 MB Octal PSRAM)

  This is the ESP-IDF counterpart of sketches/esp32_benchmark/esp32_benchmark.ino.
  It benchmarks tflite::MicroInterpreter::Invoke() on a zero input tensor (no
  camera) and prints the same block the Arduino sketch prints, so ESP-IDF and
  Arduino-core numbers can be compared model-for-model on the same board.

  Every boot also self-labels its run:
    ### <model> <WxH>  stack=idf  opt=..  psram=..MHz  cpu=..MHz  note=..
    ... human-readable block ...
    CSV,idf,<model>,<W>,<H>,<status>,<arena_loc>,<mean>,<min>,<max>,<arena_used>,<first_out>,<opt>,<psram_mhz>,<cpu_mhz>,<note>
  so a single `idf.py monitor | tee -a results/device_results.txt` needs no
  per-run typing, and `grep '^CSV,'` yields a clean table. The CSV line is emitted
  on every exit path, including allocation / OOM failures.

  Build: see BENCH_IDF_PORT_PLAN.md at the repo root.
    esp_idf && cd bench && idf.py build flash monitor   # console = UART0 @115200

  =========================================================================
   EDIT THIS BLOCK PER MODEL, then rebuild.
   All 12 headers live under main/models/<backbone>_<size>/.
   NOTE: for the rectangular sizes the header FILENAME uses 'x' but the C array
   SYMBOL inside uses '_'  (model_320x240_int8.h  ->  model_320_240_int8).

     header file                                   #define BENCH_MODEL_SYM
     models/<backbone>_64/model_64_int8.h           model_64_int8
     models/<backbone>_96/model_96_int8.h           model_96_int8
     models/<backbone>_128/model_128_int8.h         model_128_int8
     models/<backbone>_160/model_160_int8.h         model_160_int8
     models/<backbone>_320x240/model_320x240_int8.h model_320_240_int8
     models/<backbone>_480x320/model_480x320_int8.h model_480_320_int8
   <backbone> is  custom  or  mobilenetv3.
  =========================================================================*/
#include "models/mobilenetv3_64/model_64_int8.h"
#define BENCH_MODEL_SYM  model_64_int8
#define BENCH_NAME       "mobilenetv3_64"
#define BENCH_W          64
#define BENCH_H          64
#ifndef BENCH_NOTE
#define BENCH_NOTE       "manual_run1"      /* free text to tag a batch, e.g. "manual" / "selector" */
#endif
/* ======================================================================= */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ---- build fingerprint, read from Kconfig at compile time ----------------
// (undefined CONFIG_* symbols evaluate to 0 in #if)
#if   CONFIG_COMPILER_OPTIMIZATION_PERF
#  define BENCH_OPT "O2"
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
#  define BENCH_OPT "Os"
#elif CONFIG_COMPILER_OPTIMIZATION_DEBUG
#  define BENCH_OPT "Og"
#elif CONFIG_COMPILER_OPTIMIZATION_NONE
#  define BENCH_OPT "O0"
#else
#  define BENCH_OPT "O?"
#endif

#ifdef CONFIG_SPIRAM_SPEED
#  define BENCH_PSRAM_MHZ CONFIG_SPIRAM_SPEED
#else
#  define BENCH_PSRAM_MHZ 0
#endif

#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#  define BENCH_CPU_MHZ CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#else
#  define BENCH_CPU_MHZ 0
#endif

// Arena is deliberately oversized and PSRAM-backed. Its size does NOT affect
// Invoke() latency or arena_used_bytes() — TFLM only touches what it needs; it
// just has to exceed the model's true requirement (the kit's arena_est_kb is
// ~2.5x low: custom_64 estimated 34 KB, actually needs ~85 KB). The real
// footprint is reported below as "Arena used" / CSV arena_used_bytes.
#ifndef BENCH_ARENA_KB
#  if   BENCH_W * BENCH_H <= 160 * 160     // square models 64..160
#    define BENCH_ARENA_KB 2048
#  elif BENCH_W * BENCH_H <= 320 * 240     // 320x240
#    define BENCH_ARENA_KB 4096
#  else                                    // 480x320
#    define BENCH_ARENA_KB 6144
#  endif
#endif
static constexpr int kTensorArenaSize = BENCH_ARENA_KB * 1024;

static uint8_t *tensor_arena = nullptr;

struct BenchResult {
  const char *status    = "OK";   // OK | ARENA_ALLOC_FAIL | SCHEMA_MISMATCH
                                  //    | ALLOCATE_TENSORS_FAIL | INVOKE_FAIL
  const char *arena_loc = "-";     // PSRAM | SRAM | -
  int64_t mean_us = 0, min_us = 0, max_us = 0;
  uint32_t arena_used = 0;
  int first_out = 0;
};

// Runs the benchmark, printing the existing human-readable block as it goes and
// filling `r`. Sets r.status and returns early on any failure.
static void measure(BenchResult &r) {
  printf("=== ESP32-S3 Victim Benchmark ===\n");
  printf("Model: %s %dx%d\n", BENCH_NAME, BENCH_W, BENCH_H);
  printf("Arena cap: %d KB\n", kTensorArenaSize / 1024);

  // Prefer PSRAM for the arena, fall back to internal RAM.
  // (CONFIG_SPIRAM_USE_MALLOC=y is set, so MALLOC_CAP_SPIRAM is served.)
  tensor_arena = static_cast<uint8_t *>(heap_caps_malloc_prefer(
      kTensorArenaSize, 2,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (tensor_arena == nullptr) {
    printf("Arena alloc FAILED (%d KB) — enable PSRAM or pick a smaller model\n",
           kTensorArenaSize / 1024);
    r.status = "ARENA_ALLOC_FAIL";
    return;
  }
  r.arena_loc = esp_ptr_external_ram(tensor_arena) ? "PSRAM" : "SRAM";
  printf("Arena in %s\n", r.arena_loc);

  const tflite::Model *model = tflite::GetModel(BENCH_MODEL_SYM);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    printf("Model schema mismatch! (%u != %d)\n",
           static_cast<unsigned>(model->version()), TFLITE_SCHEMA_VERSION);
    r.status = "SCHEMA_MISMATCH";
    return;
  }

  // Exact union of builtin ops across all 12 shipped model_*_int8.tflite files
  // (extracted from each flatbuffer's operator_codes table):
  //   custom Tiny CNN : CONV_2D, MAX_POOL_2D, MEAN, FULLY_CONNECTED, LOGISTIC
  //   MobileNetV2 0.35: CONV_2D, DEPTHWISE_CONV_2D, ADD, MEAN, FULLY_CONNECTED, LOGISTIC
  // Re-check this list if the models are regenerated.
  static tflite::MicroMutableOpResolver<7> resolver;
  resolver.AddAdd();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddLogistic();
  resolver.AddMaxPool2D();
  resolver.AddMean();

  static tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                              kTensorArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    printf("AllocateTensors FAILED — model too large for arena (record as OOM)\n");
    r.status = "ALLOCATE_TENSORS_FAIL";
    return;
  }

  TfLiteTensor *input = interpreter.input(0);
  printf("Input shape: %d %d %d %d  dtype=%d\n",
         input->dims->data[0], input->dims->data[1],
         input->dims->data[2], input->dims->data[3],
         static_cast<int>(input->type));
  r.arena_used = interpreter.arena_used_bytes();
  printf("Arena used: %u bytes\n", static_cast<unsigned>(r.arena_used));
  printf("Fits internal SRAM (~320 KB): %s\n",
         r.arena_used <= 320u * 1024u ? "yes" : "no");

  // int8 input; 0 maps near the center of the quantized range.
  memset(input->data.int8, 0, input->bytes);

  const int WARMUP = 10, RUNS = 100;
  for (int i = 0; i < WARMUP; i++) {
    interpreter.Invoke();
    // >=1 full RTOS tick so IDLE gets to run: tick = 10 ms at CONFIG_FREERTOS_HZ=100,
    // so pdMS_TO_TICKS(<10) would round to 0 (no yield) and starve the task WDT.
    vTaskDelay(1);
  }

  int64_t t_sum = 0, t_min = INT64_MAX, t_max = 0;
  for (int i = 0; i < RUNS; i++) {
    int64_t t0 = esp_timer_get_time();
    TfLiteStatus s = interpreter.Invoke();
    int64_t dt = esp_timer_get_time() - t0;
    if (s != kTfLiteOk) {
      printf("Invoke failed %d at %d\n", static_cast<int>(s), i);
      r.status = "INVOKE_FAIL";
      return;
    }
    t_sum += dt;
    if (dt < t_min) t_min = dt;
    if (dt > t_max) t_max = dt;
    vTaskDelay(1);  // >=1 tick real yield (see warmup loop) — outside the timed region
  }
  r.mean_us = t_sum / RUNS;
  r.min_us  = t_min;
  r.max_us  = t_max;

  // Print ms with 2 decimals using integer math (no %f — robust under newlib-nano).
  printf("Runs: %d  mean: %lld.%02lld ms  min: %lld.%02lld ms  max: %lld.%02lld ms\n",
         RUNS,
         r.mean_us / 1000, (r.mean_us % 1000) / 10,
         r.min_us  / 1000, (r.min_us  % 1000) / 10,
         r.max_us  / 1000, (r.max_us  % 1000) / 10);
  printf("Hint: copy this block and send back. Note if AllocateTensors failed.\n");

  TfLiteTensor *output = interpreter.output(0);
  r.first_out = static_cast<int>(output->data.int8[0]);
  printf("Output bytes=%d  first=%d\n",
         static_cast<int>(output->bytes), r.first_out);
}

static void run_benchmark() {
  printf("\n### %s %dx%d  stack=idf  opt=%s  psram=%dMHz  cpu=%dMHz  note=%s\n",
         BENCH_NAME, BENCH_W, BENCH_H,
         BENCH_OPT, BENCH_PSRAM_MHZ, BENCH_CPU_MHZ, BENCH_NOTE);

  BenchResult r;
  measure(r);

  // Machine-readable line — always emitted, even on failure.
  printf("CSV,idf,%s,%d,%d,%s,%s,"
         "%lld.%02lld,%lld.%02lld,%lld.%02lld,%u,%d,%s,%d,%d,%s\n",
         BENCH_NAME, BENCH_W, BENCH_H, r.status, r.arena_loc,
         r.mean_us / 1000, (r.mean_us % 1000) / 10,
         r.min_us  / 1000, (r.min_us  % 1000) / 10,
         r.max_us  / 1000, (r.max_us  % 1000) / 10,
         static_cast<unsigned>(r.arena_used), r.first_out,
         BENCH_OPT, BENCH_PSRAM_MHZ, BENCH_CPU_MHZ, BENCH_NOTE);
}

static void bench_task(void *) {
  run_benchmark();
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

extern "C" void app_main() {
  // Dedicated task with an 8 KB stack — the default main-task stack
  // (CONFIG_ESP_MAIN_TASK_STACK_SIZE) is too small for AllocateTensors() on the
  // larger graphs.
  xTaskCreate(bench_task, "bench", 8192, nullptr, 5, nullptr);
}
