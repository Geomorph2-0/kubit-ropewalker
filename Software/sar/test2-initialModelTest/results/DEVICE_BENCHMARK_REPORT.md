# Victim vs NotVictim — On-Device Benchmark (ESP-IDF + Arduino, ESP32-S3)

Companion to [`BENCHMARK_REPORT.md`](BENCHMARK_REPORT.md), which covers the
dataset, backbones, training/quantization pipeline, and host-side (XNNPACK)
latency/accuracy numbers — not repeated here. This report covers the piece
that document flagged as outstanding ("Friend on-device expected — to
confirm"): real ESP32-S3 hardware timings for all 12 models, on **two**
firmware stacks, from `results/device_merged.csv` and the raw run log
`results/device_results.txt`. Full stack-port rationale and build-config
derivation is in [`BENCH_IDF_PORT_PLAN.md`](../BENCH_IDF_PORT_PLAN.md).

## Target hardware

**Freenove ESP32-S3 WROOM `N16R8`** — the physical board both sweeps ran on:

| Spec | Value |
|---|---|
| SoC | ESP32-S3 (Xtensa LX7 dual-core) |
| Flash | 16 MB |
| PSRAM | 8 MB, Octal (OPI) |
| CPU frequency (both firmwares) | 240 MHz |
| PSRAM clock (both firmwares) | 80 MHz OCT |
| Console | UART0 via the onboard CP2102 USB-UART bridge — **not** the native-USB
  port. Native USB-CDC drops off the bus on every reset/flash, which breaks
  monitor capture; UART0 survives resets and is the one console path shared
  identically by both firmwares. |

## Firmware configs — ESP-IDF vs Arduino

The two stacks were **deliberately matched** on the axes that most affect
absolute latency (PSRAM speed, CPU clock) and **left unmatched** on axes that
are themselves part of what's being compared (toolchain, optimization level,
scheduler tick). Both are logged in every `CSV,` row (`opt`, `psram_mhz`,
`cpu_mhz` fields) so the config is machine-checkable per run, not just
asserted here.

| Axis | ESP-IDF (`bench/`) | Arduino (`sketches/esp32_benchmark/`) | Matched? |
|---|---|---|---|
| Toolchain | ESP-IDF v6.1 (`idf.py`) | arduino-esp32 core 3.3.7 | — (the variable under test) |
| Optimization level | `-O2` ("Optimize for performance", menuconfig) | `-Os` (Arduino has no `-O2` menu for esp32s3) | **No** — confound, see below |
| CPU frequency | 240 MHz | 240 MHz | Yes |
| PSRAM speed/mode | 80 MHz, Octal | 80 MHz, Octal (OPI) | Yes |
| FreeRTOS tick rate | 100 Hz (`CONFIG_FREERTOS_HZ=100`) | 1000 Hz (core default) | **No** — see WDT note below |
| TFLite-Micro | esp-tflite-micro **1.4.0**, source-built | esp-tflite-micro **1.3.5**, precompiled in core (`libespressif__esp-tflite-micro.a`) | — |
| esp-nn kernels | **1.3.1**, source-built with `-O2` | **1.1.2**, precompiled | — |
| Op resolver | `MicroMutableOpResolver<7>`: Add, Conv2D, DepthwiseConv2D, FullyConnected, Logistic, MaxPool2D, Mean | identical 7-op set | Yes |
| Arena location | PSRAM (`heap_caps_malloc_prefer`, oversized ladder) | PSRAM (`ps_malloc`, oversized ladder) | Yes (both landed `PSRAM` on all 12 runs) |
| Flash / partition | 16 MB flash, default IDF partition | 16 MB flash, "Huge APP" (≥3 MB app) scheme | Yes (both fit; see `BENCH_IDF_PORT_PLAN.md` — 449 KB–1035 KB per model, compile-checked) |
| Input | zero-filled `int8` tensor, `WARMUP=10`, `RUNS=100` | identical | Yes |

**Confound #1 — optimization level.** `custom` (pure-C Tiny CNN, no
precompiled kernels) is the model most exposed to `-O2` vs `-Os`;
`mobilenetv3` runs almost entirely inside precompiled esp-nn kernels on
*both* stacks, so the flag matters far less there. This shows up directly in
the results below.

**Confound #2 — FreeRTOS tick rate.** IDF's 100 Hz tick meant the firmware's
original `vTaskDelay(pdMS_TO_TICKS(2))` between invokes rounded to
`vTaskDelay(0)` — not a real yield — starving `IDLE0` across a 100-run loop
and tripping the task watchdog mid-benchmark on the very first IDF run. Fixed
by switching to `vTaskDelay(1)` (one full tick = guaranteed yield). Arduino's
1000 Hz tick made the equivalent `delay(2)` a real yield from the start, so
it never hit this. Not a stack-performance difference — a benchmark-harness
bug, now fixed — but it did contaminate one logged run (see next section).

## Full results (all 12 models)

Sorted by backbone, then pixel count — same order as `device_merged.csv`.
`mean/min/max` are over `RUNS=100` (after 10 warmup runs); `Arena est.` is
the static estimate from `results.csv`; `Arena KB` is the actual
`arena_used_bytes()` reported by TFLite-Micro on-device (authoritative).

| Backbone | Size | Test Acc | Test AUC | ArenaEst KB | IDF mean/min/max ms | IDF arena KB | Arduino mean/min/max ms | Arduino arena KB | Δ idf−arduino | Arena est vs actual |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| custom | 64 | 70.2% | 0.794 | 33.6 | 84.03 / 84.03 / 84.03 | 82.9 | 88.55 / 88.55 / 88.56 | 94.0 | -4.52 ms (-5.1%) | -59.5% |
| custom | 96 | 71.9% | 0.824 | 75.6 | 187.07 / 187.06 / 187.08 | 182.9 | 197.53 / 197.53 / 197.53 | 204.4 | -10.46 ms (-5.3%) | -58.7% |
| custom | 128 | 73.7% | 0.818 | 134.4 | 446.54 / 446.53 / 446.55 | 323.7 | 591.46 / 591.45 / 591.46 | 359.5 | -144.92 ms (-24.5%) | -58.5% |
| custom | 160 | 75.4% | 0.831 | 210.0 | 695.18 / 695.17 / 695.18 | 503.7 | 939.94 / 939.93 / 939.94 | 557.9 | -244.76 ms (-26.0%) | -58.3% |
| custom | 320x240 | 80.7% | 0.871 | 630.0 | 2362.65 / 2362.64 / 2362.66 | 1504.7 | 2817.29 / 2817.27 / 2817.34 | 1660.3 | -454.64 ms (-16.1%) | -58.1% |
| custom | 480x320 | 77.2% | 0.867 | 1260.0 | 4725.79 / 4725.77 / 4725.80 | 3004.7 | 5677.24 / 5677.19 / 5677.27 | 3311.7 | -951.45 ms (-16.8%) | -58.1% |
| mobilenetv3 | 64 | 75.4% | 0.823 | 48.0 | 101.85 / 101.84 / 101.85 | 210.5 | 101.66 / 101.65 / 101.68 | 210.5 | +0.19 ms (+0.2%) | -77.2% |
| mobilenetv3 | 96 | 87.7% | 0.954 | 108.0 | 169.74 / 169.74 / 169.75 | 219.1 | 177.66 / 177.66 / 177.68 | 317.7 | -7.92 ms (-4.5%) | -50.7% |
| mobilenetv3 | 128 | 87.7% | 0.968 | 192.0 | 274.49 / 274.47 / 274.50 | 314.8 | 283.46 / 283.46 / 283.47 | 509.7 | -8.97 ms (-3.2%) | -39.0% |
| mobilenetv3 | 160 | 89.5% | 0.967 | 300.0 | 434.07 / 434.06 / 434.09 | 452.0 | 453.13 / 453.12 / 453.13 | 755.7 | -19.06 ms (-4.2%) | -33.6% |
| mobilenetv3 | 320x240 | 91.2% | 0.990 | 900.0 | 1284.23 / 1284.21 / 1284.24 | 1213.3 | 1340.72 / 1340.71 / 1340.76 | 2116.9 | -56.49 ms (-4.2%) | -25.8% |
| mobilenetv3 | 480x320 | 94.7% | 0.990 | 1800.0 | 2644.72 / 2644.70 / 2644.75 | 2349.5 | 2663.10 / 2663.08 / 2663.11 | 4153.2 | -18.38 ms (-0.7%) | -23.4% |

*All 12 models `status=OK` on both stacks; arena landed in PSRAM on both
stacks for every model (no `ARENA_ALLOC_FAIL`/`ALLOCATE_TENSORS_FAIL` in the
final numbers — see next section for the runs that did fail along the way).*

## Run-history / data-quality notes

`device_results.txt` is an append-only log, and it is not 12 clean lines —
it's 15 IDF rows and 12 Arduino rows, because two of the IDF models were
re-run after early problems. `collect_device_csv.py` resolves this by keeping
the **last** row per `(stack, backbone, size)` in file order, so the table
above already reflects the clean numbers — but the discarded runs are real
findings in their own right:

1. **`custom_64`, IDF run 1 — arena too small.** The original arena ladder
   allocated 60 KB for the `≤64²` tier; `custom_64` needs ~85 KB. Logged as a
   genuine `CSV,idf,custom_64,...,ALLOCATE_TENSORS_FAIL,...` row
   (`Failed to resize buffer. Requested: 81920, available 58600, missing:
   23320`). Fixed by moving to the current oversized, PSRAM-backed arena
   ladder.
2. **`custom_64`, IDF run 2 — watchdog-tainted `max`.** With the arena fixed,
   this run completed `OK` (`mean=85.85ms, min=85.51ms`) but `max=118.62ms` is
   inflated by the task-watchdog firing mid-run (see "Confound #2" above) —
   `mean`/`min` are still valid, `max` is not. Fixed via `vTaskDelay(1)`;
   `manual_run3` (`84.03/84.03/84.03ms`, the value in the table above)
   supersedes it.
3. **`custom_96`, IDF — harmless duplicate.** `manual_run1` and `manual_run2`
   are identical to 2 decimal places (`187.07/187.06/187.08ms` both times) —
   just a confirmation re-run, not a finding.
4. **Arduino sweep — clean.** All 12 `CSV,arduino,...` rows are single runs,
   `status=OK`, no reruns or failures. Arduino's 1000 Hz tick meant it was
   never exposed to the watchdog issue that hit IDF run 2.

## Breakdown / analysis

### IDF vs Arduino

Across all 12 models, **IDF was faster in 11/12** (only `mobilenetv3_64`
went the other way, and only by +0.19 ms / +0.2% — noise-level). Mean delta:
**-160.12 ms (-9.2%)**, IDF faster on average.

The gap is not uniform — it tracks the optimization-level confound directly:

- **`custom` (pure-C, no precompiled kernels):** gap grows with model size,
  from -5.1% at 64² to **-26.0% at 160²**, moderating slightly at the two
  native resolutions (-16.1%, -16.8%). `-O2` vs `-Os` on hand-written C is
  exactly where a compiler flag should matter most, and it does.
- **`mobilenetv3` (dominated by precompiled esp-nn kernels on both
  stacks):** gap stays small and flat, -4.5% to +0.2%, regardless of
  resolution. The optimization flag only affects the thin glue code around
  the kernels, so it barely moves the needle — consistent with
  `BENCH_IDF_PORT_PLAN.md`'s prediction that esp-nn (precompiled in *both*
  stacks) is the dominant cost, not the wrapping toolchain.

A second, unplanned observation: **actual arena usage differs by stack for
the same model**, not just the estimate-vs-actual gap. For `mobilenetv3`,
Arduino's measured arena runs 25–50% *larger* than IDF's for the same model
(e.g. `mobilenetv3_160`: 452.0 KB IDF vs 755.7 KB Arduino) — plausibly the
esp-tflite-micro version difference (1.4.0 vs 1.3.5) changing internal
allocator/scratch-buffer behavior. `custom` shows a smaller, more consistent
gap (~11–14%). This means arena sizing must be validated per-stack, not just
per-model — a number safe for IDF is not automatically safe for Arduino.

### Arena estimate vs device-measured

`results.csv`'s static `arena_est_kb` (used at generation time) undershoots
the real on-device figure for **every single model**, not just the one
`custom_64` example `BENCHMARK_REPORT.md` originally called out:

- Mean underestimate: **-50.1%** (device is ~2× the estimate, on average).
- Worst: **`mobilenetv3_64`, -77.2%** — actual arena is ~4.4× the estimate
  (48 KB estimated vs 210.5 KB actual).
- Best (least-off): **`mobilenetv3_480x320`, -23.4%**.
- `custom` is remarkably consistent regardless of size: -58.1% to -59.5%
  across all six resolutions — suggesting the estimator's error is closer to
  a fixed multiplicative factor for this backbone than a resolution-dependent
  one.

**Takeaway:** treat `arena_est_kb` as a lower bound only. `arena_used_bytes`
in `device_merged.csv` (from either stack) is the number to size a real
deployment's arena against.

### Device vs host latency

`BENCHMARK_REPORT.md` guessed *"ESP32 ~100× slower"* than the host XNNPACK
benchmark. With both numbers now in the same table, the real ratio (IDF
mean ÷ host mean) is:

| Model | Ratio | Model | Ratio |
|---|---:|---|---:|
| custom_64 | 672× | mobilenetv3_64 | 834× |
| custom_96 | 221× | mobilenetv3_96 | 241× |
| custom_128 | 658× | mobilenetv3_128 | 471× |
| custom_160 | 461× | mobilenetv3_160 | 407× |
| custom_320x240 | 641× | mobilenetv3_320x240 | 516× |
| custom_480x320 | 523× | mobilenetv3_480x320 | 347× |

Mean **~499×**, ranging 221×–834× — not a flat 100× as guessed, and not
tightly correlated with resolution either (the 96² tier is the low outlier
for both backbones, likely an XNNPACK-side host anomaly at that size rather
than an ESP32 effect — `BENCHMARK_REPORT.md` already flagged `mobilenetv3@128`
host timing as an "XNNPACK anomaly"). **Host latency should be treated as a
relative ranking signal only, not a device-latency predictor** — the original
report's own caveat, now with real numbers behind it.

## Updated key takeaways

Replacing `BENCHMARK_REPORT.md` §4 ("Friend on-device expected — to
confirm") with confirmed results:

- **Custom 64–96 on real hardware: 84–198 ms**, not the "<150ms" guess —
  `custom_96` slightly exceeds it on Arduino (197.53 ms). Both are well clear
  of any watchdog/timeout risk once the tick-rate fix is applied.
- **No OOM on any model, on either stack**, including 320×240/480×320 —
  the oversized PSRAM arena ladder (added specifically to avoid the
  originally-anticipated `AllocateTensors FAILED` at native resolution)
  worked; the only `ALLOCATE_TENSORS_FAIL` observed all sweep was the
  since-fixed pre-ladder `custom_64` run, not a native-resolution model.
- **mobilenetv3 @96 or @128 remains the best accuracy/latency trade**, now
  with real numbers: 87.7% test acc at **170–187 ms** (IDF) / **178–283 ms**
  (Arduino) — squarely usable for a periodic (not per-frame) inference
  cadence.
- **custom @96 remains the flash/PSRAM-constrained choice**: 71.9% test acc,
  **187–198 ms**, 36 KB flash — at ~5–14% slower than mobilenetv3@96 in wall
  time despite 1/17th the flash, the accuracy trade (71.9% vs 87.7%) is the
  real cost, not latency.
- **Native resolution (320×240/480×320) is confirmed expensive on-device
  too**: 1.28–5.68 **seconds** per inference (not ms) — 2.4–3.5 GHz-host was
  ms-scale, the >2× arena discrepancy vs the ms/MB proxy at training time
  actually understated how much this hurts. Confirms the original
  recommendation: **letterbox-resize to 96/128 at capture for deployment;
  keep native models for the ablation table only.**

## Suggested additions

Recommendations, not reported data:

1. **Repeat runs (N>1) per model per stack.** Every number above is from a
   single 100-invoke run; there's no run-to-run variance/error bar. Cheap to
   add — `run_sweep.sh` already supports re-running with a different `note`.
2. **Device-latency plot.** `results/plot_latency_vs_resolution.png` exists
   for host data only; an equivalent device-side plot (idf + arduino, log
   latency vs `W×H`) would make the "1.3–5.7 seconds at native resolution"
   finding visually obvious next to the sub-200ms low-res numbers.
3. **Isolate the optimization-level confound.** Run IDF at `-Os` (or Arduino
   via PlatformIO at `-O2`) for at least the `custom` backbone, to separate
   "toolchain/scheduler difference" from "compiler flag difference" — right
   now they're bundled into one `idf` vs `arduino` delta.
4. **Real camera input, not a zero tensor.** Current runs use a memset-zero
   `int8` input — valid for latency (compute path doesn't depend on pixel
   values for these ops) but says nothing about on-device inference
   *correctness*/accuracy. A short capture-and-classify sanity pass against a
   few known images would close that gap.
5. **Explain the per-stack arena-size discrepancy.** `mobilenetv3`'s
   25–50% larger measured arena on Arduino vs IDF (same model, same board) is
   unexplained here — worth a quick diff of esp-tflite-micro 1.3.5 vs 1.4.0
   allocator behavior if arena headroom ever becomes tight.
6. **Flash-partition fit, from real binaries.** `BENCH_IDF_PORT_PLAN.md`
   notes 449 KB–1035 KB compiled Arduino binaries fit the 3 MB app partition
   with room to spare — worth a one-line per-model table (`idf.py size` /
   `arduino-cli compile --show-properties`) now that all 12 build cleanly, to
   catch any future model that gets close to the ceiling.
7. **Power/current draw**, if battery life matters for deployment — not
   measured in either sweep; would need a shunt/INA219 in the loop, out of
   scope for this pass.
