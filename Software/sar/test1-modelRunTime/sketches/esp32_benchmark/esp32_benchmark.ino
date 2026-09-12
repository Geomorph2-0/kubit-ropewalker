/*
  esp32_benchmark.ino — On-device latency for Victim vs NotVictim (MobileNetV2 / Custom)
  Target: ESP32-S3 with PSRAM (Freenove ESP32-S3 WROOM N16R8 in this kit).

  This is the Arduino-core counterpart of bench/main/main.cpp (the ESP-IDF build).
  Same 16-column CSV schema, same "### ..." self-label header, same arena ladder,
  so the two stacks can be compared line-for-line.

  ---- running it (one model at a time) ------------------------------------
    ./select_model.sh <backbone> <size>     # custom|mobilenetv3  64|96|128|160|320x240|480x320
      (rewrites the 5 EDIT-PER-MODEL #defines below in one shot — header path,
       array symbol, W, H, label — so they can never drift apart)
    then: Sketch > Upload  ->  Serial Monitor @ 115200  ->  copy the
    "### ... / CSV,arduino,..." block into results/device_results.txt.
    Repeat for each of the 12 models.
    Full arduino-cli steps: BENCH_IDF_PORT_PLAN.md > "Arduino cross-stack sweep".

  ---- TFLite-Micro library --------------------------------------------------
    Uses the esp-tflite-micro + esp-nn archives BUNDLED with the arduino-esp32
    3.x core (no library to install — the "TFLite Micro" entry in Library
    Manager is just an example stub). Versions in core 3.3.7:
      espressif__esp-tflite-micro 1.3.5   (IDF build: 1.4.0)
      espressif__esp-nn           1.1.2   (IDF build: 1.3.1)
    CSV rows self-tag  note=esptflm:<batch>  so they sit unambiguously beside the
    idf rows in results/device_results.txt.
    NOTE: the kit's old GUIDE.md says to install "TensorFlowLite_ESP32"
    (tanakamasayuki) — that library is abandoned (v1.0.0) and does NOT compile
    with core 3.x. This sketch does not include it, so it's harmless if already
    installed; only if you somehow hit a TFLM include/link error, move it aside:
      mv ~/Arduino/libraries/TensorFlowLite_ESP32{,.off}

  ---- Tools menu (must match the IDF build for a fair compare) --------------
    Board            : ESP32S3 Dev Module   (or "Freenove ESP32-S3")
    CPU Frequency    : 240 MHz
    Flash Size       : 16 MB
    PSRAM            : OPI PSRAM            (core builds this at 80 MHz OCT = same as IDF)
    USB CDC On Boot  : Disabled            (-> Serial = UART0; plug into the Type-C
                                             port next to the CP2102 chip, silk
                                             "UART", = /dev/ttyUSB0. That port also
                                             matches the IDF console and its link
                                             survives resets. Native-USB port +
                                             CDC On Boot Enabled + /dev/ttyACM0
                                             works too, but the CDC endpoint drops
                                             off the bus on every reset.)
    USB Mode         : Hardware CDC and JTAG   (default — leave it)
    Partition Scheme : any with >= 3 MB app    (e.g. "Huge APP")
  There is no user Optimize menu for esp32s3 — the core compiles the sketch at
  -Os; the esp-tflite-micro / esp-nn kernels are Espressif-precompiled (see
  benchOpt / the CSV opt field). This benchmark uses a zero input tensor.
*/

// ===== EDIT PER MODEL (or run ./select_model.sh) ==========================
#define BENCH_MODEL_HDR  "/home/rbj/Downloads/friend_esp32_kit_v2/models/mobilenetv3_480x320/model_480x320_int8.h"
#define BENCH_MODEL_SYM  model_480_320_int8   // array name INSIDE that header ('_' not 'x')
#define MODEL_W          480
#define MODEL_H          320
#define BACKBONE         "mobilenetv3_480x320"
// =========================================================================
#ifndef BENCH_NOTE
#define BENCH_NOTE "manual"    // free text to tag a batch, e.g. "manual" / "selector"
#endif
#define BENCH_LIB_TAG "esptflm"   // core-bundled esp-tflite-micro (see header)

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include BENCH_MODEL_HDR                                         // defines BENCH_MODEL_SYM[]

// ---- PSRAM clock (gates every number — arenas are all PSRAM-backed) ------
// arduino-esp32 puts sdkconfig.h on the include path; read what the core built.
// Core 3.3.7 qio_opi builds CONFIG_SPIRAM_SPEED=80 → matches the IDF build.
#if   defined(CONFIG_SPIRAM_SPEED) && (CONFIG_SPIRAM_SPEED + 0) > 0
#  define BENCH_PSRAM_MHZ (CONFIG_SPIRAM_SPEED + 0)
#elif defined(CONFIG_SPIRAM_SPEED_120M)
#  define BENCH_PSRAM_MHZ 120
#elif defined(CONFIG_SPIRAM_SPEED_80M)
#  define BENCH_PSRAM_MHZ 80
#elif defined(CONFIG_SPIRAM_SPEED_40M)
#  define BENCH_PSRAM_MHZ 40
#elif defined(CONFIG_SPIRAM_SPEED_26M)
#  define BENCH_PSRAM_MHZ 26
#elif defined(CONFIG_SPIRAM_SPEED_20M)
#  define BENCH_PSRAM_MHZ 20
#else
#  define BENCH_PSRAM_MHZ 0
#endif

// ---- self-labelling output (matches bench/main/main.cpp) ----
static const char* g_status    = "OK";   // OK | ARENA_ALLOC_FAIL | SCHEMA_MISMATCH
                                         //    | ALLOCATE_TENSORS_FAIL | INVOKE_FAIL
static const char* g_arena_loc = "-";    // PSRAM | SRAM | -
static double g_mean_ms = 0, g_min_ms = 0, g_max_ms = 0;
static size_t g_arena_used = 0;
static int    g_first_out  = 0;

// NOTE: reflects only THIS translation unit (-Os on core 3.3.7). The
// esp-tflite-micro / esp-nn kernels doing the actual work are Espressif-
// precompiled, so this field does NOT describe them.
static const char* benchOpt() {
#if defined(__OPTIMIZE_SIZE__)
  return "Os";
#elif defined(__OPTIMIZE__)
  return "O2";
#else
  return "O0";
#endif
}

static void emitCsv() {
  Serial.printf("CSV,arduino,%s,%d,%d,%s,%s,%.2f,%.2f,%.2f,%u,%d,%s,%d,%d,%s:%s\n",
    BACKBONE, MODEL_W, MODEL_H, g_status, g_arena_loc,
    g_mean_ms, g_min_ms, g_max_ms,
    (unsigned)g_arena_used, g_first_out,
    benchOpt(), (int)BENCH_PSRAM_MHZ, (int)getCpuFrequencyMhz(),
    BENCH_LIB_TAG, BENCH_NOTE);
}

// Arena is deliberately oversized and PSRAM-backed. Its size does NOT affect
// Invoke() latency or arena_used_bytes(); it just has to exceed the model's true
// need (the kit's arena_est_kb is ~2.5x low). The real footprint is the
// "Arena used" line / CSV arena_used_bytes. Kept identical to bench/main/main.cpp.
// Measured arena_used (device): custom_160 516 KB, custom_320x240 1.54 MB,
// custom_480x320 3.08 MB. Tiers below clear those with margin. Top tier is 5 MB
// (not 6) — a 6 MB ps_malloc on the 8 MB chip risks failing under the Arduino
// core's PSRAM reserve; the IDF build uses heap_caps_malloc_prefer and can take 6.
#ifndef BENCH_ARENA_KB
#  if   MODEL_W * MODEL_H <= 160*160     // square models 64..160
#    define BENCH_ARENA_KB 2048
#  elif MODEL_W * MODEL_H <= 320*240     // 320x240
#    define BENCH_ARENA_KB 4096
#  else                                  // 480x320
#    define BENCH_ARENA_KB 5120
#  endif
#endif
constexpr int kTensorArenaSize = BENCH_ARENA_KB * 1024;
uint8_t* tensor_arena = nullptr;

void setup() {
  Serial.begin(115200);
  delay(3500);   // gives `arduino-cli monitor` time to attach after the
                 // post-upload reset. On the CP2102 port the link
                 // survives resets so this is slack, not a hard requirement;
                 // if a block is ever missing, tap RESET — setup() replays it.
  Serial.printf("\n### %s %dx%d  stack=arduino  opt=%s  psram=%dMHz  cpu=%dMHz  note=%s:%s\n",
    BACKBONE, MODEL_W, MODEL_H, benchOpt(), (int)BENCH_PSRAM_MHZ,
    (int)getCpuFrequencyMhz(), BENCH_LIB_TAG, BENCH_NOTE);
  Serial.println("=== ESP32-S3 Victim Benchmark ===");
  Serial.printf("Model: %s %dx%d  lib=%s\n", BACKBONE, MODEL_W, MODEL_H, BENCH_LIB_TAG);
  Serial.printf("Arena cap: %d KB  PSRAM? %s\n", kTensorArenaSize/1024, psramFound() ? "yes" : "no");

  if (psramFound()) {
    tensor_arena = (uint8_t*) ps_malloc(kTensorArenaSize);
    Serial.println("Arena in PSRAM");
  } else {
    tensor_arena = (uint8_t*) malloc(kTensorArenaSize);
    Serial.println("Arena in SRAM");
  }
  g_arena_loc = psramFound() ? "PSRAM" : "SRAM";
  if (!tensor_arena) { Serial.println("Arena alloc FAILED! Reduce model or enable PSRAM"); g_status = "ARENA_ALLOC_FAIL"; emitCsv(); return; }

  const tflite::Model* model = tflite::GetModel(BENCH_MODEL_SYM);
  if (model->version() != TFLITE_SCHEMA_VERSION) { Serial.println("Model schema mismatch!"); g_status = "SCHEMA_MISMATCH"; emitCsv(); return; }

  // Op union across all 12 kit models — identical set to bench/main/main.cpp.
  static tflite::MicroMutableOpResolver<7> resolver;
  resolver.AddAdd();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddLogistic();
  resolver.AddMaxPool2D();
  resolver.AddMean();

  static tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kTensorArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) { Serial.println("AllocateTensors FAILED — model too large for arena"); g_status = "ALLOCATE_TENSORS_FAIL"; emitCsv(); return; }

  Serial.printf("Input shape: %d %d %d %d  dtype=%d\n",
    interpreter.input(0)->dims->data[0], interpreter.input(0)->dims->data[1],
    interpreter.input(0)->dims->data[2], interpreter.input(0)->dims->data[3],
    interpreter.input(0)->type);
  g_arena_used = interpreter.arena_used_bytes();
  Serial.printf("Arena used: %d bytes\n", (int)interpreter.arena_used_bytes());

  // Benchmark
  const int WARMUP=10, RUNS=100;
  TfLiteTensor* input = interpreter.input(0);
  // int8 input: 0 is a valid mid-range code once the zero-point is applied.
  memset(input->data.int8, 0, input->bytes);

  for(int i=0;i<WARMUP;i++) interpreter.Invoke();

  int64_t t_sum=0, t_min=INT64_MAX, t_max=0;
  for(int i=0;i<RUNS;i++){
    int64_t t0 = esp_timer_get_time();
    TfLiteStatus s = interpreter.Invoke();
    int64_t dt = esp_timer_get_time() - t0;
    if(s!=kTfLiteOk) { Serial.printf("Invoke failed %d at %d\n", s, i); g_status = "INVOKE_FAIL"; emitCsv(); return; }
    t_sum+=dt; if(dt<t_min) t_min=dt; if(dt>t_max) t_max=dt;
    delay(2);   // Arduino core tick = 1 kHz, so this really yields (no WDT issue)
  }
  g_mean_ms = t_sum/1000.0/RUNS; g_min_ms = t_min/1000.0; g_max_ms = t_max/1000.0;
  Serial.printf("Runs: %d  mean: %.2f ms  min: %.2f ms  max: %.2f ms\n", RUNS, g_mean_ms, g_min_ms, g_max_ms);

  TfLiteTensor* output = interpreter.output(0);
  g_first_out = output->data.int8[0];
  Serial.printf("Output bytes=%d  first=%d\n", output->bytes, output->data.int8[0]);
  emitCsv();
}

void loop() { delay(1000); }
