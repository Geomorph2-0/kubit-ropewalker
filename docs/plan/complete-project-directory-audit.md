# Complete Project Directory Audit — kubit-ropewalker

*Audit date: 2026-09-11. Read-only investigation — no files were modified to produce this report.*

> **Update — 2026-09-12:** controller stage 4 has since been bench-tested and passes. A real compile blocker was found and fixed first: `Software/controller-firmware/link.cpp`'s `onSent()` ESP-NOW send-callback used the pre-3.1-core signature (`const uint8_t *mac`), but the installed Arduino-ESP32 core (3.3.x) requires `const wifi_tx_info_t *`. After the fix, both boards were flashed (identity confirmed by factory MAC via `esptool read-mac` before each write — a good thing, since the first attempt grabbed the wrong physical board) and the robot's serial output showed a steady, CRC-valid packet stream from the controller (`rej=0`, sequence numbers climbing cleanly) — satisfying the stage-4 exit test in `docs/controller_plan.md` ("Robot prints packets"). This resolves §6/§7's "not yet flashed or bench-tested" status, §11's "entirely unverified against real silicon" item, §15's hard-blocker #4, and the corresponding line in §18's immediate actions. The rest of this audit's findings (robot motion/encoders/SAR pipeline absent, `esp32_camera_victim.ino`'s separate TFLite Micro API mismatch, `protocol.h`'s symlink conversion still pending, etc.) are unaffected and still current as of the audit date above.

> **Update — 2026-09-12 (2):** the motor driver is a **DRV8833**, not the TB6612FNG this audit's findings refer to throughout (§2, §3, §5.4, §11 area, §13, §15, §18) — that was accurate as of the audit date but has since been corrected across `README.md`, `docs/controller_plan.md`, `docs/hardware/esp32_devkit_38pin_pinout.md`, and `docs/hardware/esp32_motion_board_log.md`, including the driver pin list (DRV8833 drives `AIN1`/`AIN2`/`BIN1`/`BIN2` directly, no separate PWM pin, plus `nSLEEP`/`nFAULT` — not TB6612FNG's `PWMA`/`AIN1`/`AIN2`/`PWMB`/`BIN1`/`BIN2`/`STBY`). The underlying finding is unchanged: the robot-side GPIO pin map for whichever driver is fitted is still undecided.

## Context

This is a full forensic audit of the `kubit-ropewalker` repository: an evidence-based reconstruction of what exists, what works, what's incomplete, what contradicts what, and what should happen next. It was built by reading every source file, every doc, the `.gitignore`, the full git log, and the current uncommitted diff directly — not by sampling or delegating to sub-agents, because the repo is small enough for a direct read to be both feasible and more reliable for the cross-file consistency checks this audit depends on.

**Mid-audit update:** partway through, a new untracked directory, `Software/sar/`, appeared in the working tree (confirmed by file timestamps: both `test1-modelRunTime/` and `test2-initialModelTest/` were created on 2026-09-11, about a minute apart). This directory was fully read and is incorporated below — it materially changes the picture from a first pass that found no Search & Rescue software at all.

**Headline finding, revised after the `Software/sar/` discovery:** the competition (SST Makerspace 3.0, main event **5–9 October 2026**) is roughly **3–4 weeks away** as of the audit date. Per the brief's own dates, the Mock Event/Video deadline (17 Aug 2026) and Review 4 (8 Jul 2026) have already passed. Inside this repository: the **ground controller** is a disciplined, mid-build codebase at stage 4 of 6. **Victim detection** (camera + on-device ML) has real, evidence-backed engineering behind it — a 574-image dataset, 12 trained/quantized model variants, and genuine on-device benchmark results across two firmware stacks — but the piece that actually runs on a camera (`esp32_camera_victim.ino`) has not been confirmed working, and is written against a TFLite Micro API the project's own later work found broken on the current toolchain (see §11). **Robot drive, motor control, wheel encoders, HTTP upload to the competition server, and any chassis/mechanical work** still have zero footprint anywhere in this repository. Swiftplay (35% of score) has no code or requirements analysis at all. This audit cannot tell you whether that missing work exists elsewhere (another repo, a teammate, work done outside version control) — see §21.

---

## 1. Executive Summary

`kubit-ropewalker` is the software (and adjacent hardware-documentation) half of a robotics-competition entry: a rope-traversing crawler robot for SST Makerspace's "Rope Runner Challenge." The repo contains **three distinct bodies of real work**, at very different levels of integration: (1) a well-engineered, mid-build **ground controller** on a classic ESP32, currently at an uncommitted, hardware-unverified stage 4 (ESP-NOW radio link); (2) a rigorous, completed **on-device victim-detection model selection and benchmark study** (`Software/sar/test1-modelRunTime/`) — real dataset, real training, real int8 quantization, real on-device timings across 12 model variants and two firmware stacks; and (3) a **camera-based victim-inference sketch** (`Software/sar/test2-initialModelTest/`) that applies the winning model to a live camera feed but has not been confirmed to run, and is written against a library combination the project's own item-2 work found does not compile on the toolchain now in use. None of the three talks to either of the other two yet — there is no code path from "camera says victim" to "package this with encoder distance and upload it," and no code path from "robot receives a drive command" to "wheels turn."

## 2. What This Project Is

A rope-walking crawler robot that hangs from and drives along a rope strung 25 cm above the floor, entered in a specific, dated competition (SST Makerspace 3.0, main event 5–9 Oct 2026, per `README.md` and `docs/competition/SAR_requirements_extract.md`). It is scored on **Swiftplay** (speed/obstacle traversal, 35% / 140 pts), **Search & Rescue** (camera-based victim detection + encoder distance + upload to a server, 40% / 160 pts), and **Design** (mechanical/presentation, 25% / 100 pts, no corresponding files anywhere in this repo). The intended final product is a battery-powered, radio-controlled robot with: a ground controller (built), a drive/motor subsystem with wheel encoders (not started), a downward-facing camera that detects victim markers on-device (model chosen and benchmarked; live-camera integration unconfirmed) and uploads image+metadata to a server (not started), and a failsafe layer that stops the robot if the radio link is lost (not started).

## 3. Project Architecture

Three microcontrollers, per `README.md`'s hardware table and `docs/hardware/esp32_motion_board_log.md` / `esp32s3_spec_sheet.md` (all MACs independently verified on the bench, with one documented correction to an earlier byte-reversed reading):

| Role | Board | MAC | Status in this repo |
|---|---|---|---|
| Ground controller | classic ESP32-D0WD-V3, 38-pin DevKit | `F4:2D:C9:71:7B:0C` | **Actively built**, stage 4 of 6, uncommitted, unflashed |
| Robot motion | classic ESP32-D0WD-V3, 38-pin DevKit | `F4:2D:C9:71:0A:7C` | **Bring-up-test-only** — one throwaway ESP-NOW receiver sketch; no drive/motor/encoder firmware |
| Camera / upload / ML | Freenove ESP32-S3-WROOM CAM, N16R8 | `D0:CF:13:00:2C:E0` | **Hardware verified; ML model chosen and on-device-benchmarked on this exact board; live camera→inference sketch written but unconfirmed; no upload/encoder-pairing code** |

Four architectural pieces exist; three are internally coherent and one connective piece (the SAR data pipeline end-to-end) does not exist yet:

```
(1) Controller → robot control link (code-complete, hardware-unverified)
Joystick+Buttons → Control (policy) → Link (ESP-NOW @ 50Hz) → [robot: receive-only bring-up sketch, drives nothing]

(2) Victim-detection model pipeline (COMPLETE, real hardware results)
dataset (574 imgs, outside this repo) → train/quantize (outside this repo)
   → 12 int8 .tflite models → on-device benchmark (ESP-IDF + Arduino, Freenove N16R8)
   → winner selected: mobilenetv3_96 @ threshold 0.65 (87.7% test acc, ~170-198ms/inference)

(3) Camera → live inference (WRITTEN, UNCONFIRMED, likely broken on current toolchain — see §11)
camera (RGB565) → letterbox → mobilenet preprocess → int8 quantize → Invoke() → score
   → if victim: write a text marker to SD (NOT a real photo, NOT uploaded, NOT tagged, NOT paired with distance)

(4) [DOES NOT EXIST ANYWHERE IN THIS REPO]
   robot motor/encoder firmware · distance-image pairing · team-tag overlay
   · HTTP upload+retry · robot-side failsafe · any mechanical/CAD/BOM work
```

## 4. Directory Structure

```
kubit-ropewalker/                         (git root)
├── README.md                             top-level project overview (MODIFIED, uncommitted)
├── .gitignore                            excludes docs/vendor/, arduino_secrets.h, the ds4-monitor binary
├── docs/
│   ├── controller_plan.md                controller design doc + staged build guide (MODIFIED)
│   ├── competition/                      competition briefs (2 PDFs) + a hand-extracted SAR requirements doc
│   ├── hardware/                         3 spec-sheet/bring-up-log markdown files + 1 datasheet PDF (MODIFIED: pinout doc)
│   ├── images/                           2 pinout reference images
│   ├── plan/                             this audit
│   └── vendor/                           gitignored Freenove SDK, ~370 MB, redownloadable — not audited in depth (§13 "do not touch")
└── Software/
    ├── controller-firmware/              the ground controller, 10 logical modules (14 files) — MODIFIED + 5 new untracked files
    ├── s3-uart-bridge/                   1 file, untracked — temporary bring-up relay sketch
    ├── shared/                           1 file, untracked — canonical Protocol::ControlPacket definition
    ├── sar/                              NEW, untracked — victim-detection ML: benchmarking + camera-inference kits
    │   ├── test1-modelRunTime/           dataset-trained 12-model int8 benchmark, real on-device results (both stacks)
    │   └── test2-initialModelTest/       camera+inference deployment sketch (Arduino + IDF variants), unconfirmed
    ├── tests/Arduino/                    10 bring-up/experimental sketches, one folder each (1 new untracked: robot-stage4-espnow-receiver)
    └── tools/ds4-monitor/                host-side (Linux PC) C tool, unrelated to the firmware build; gitignored binary is present and stale
```

## 5. File-by-File Audit

### 5.1 Controller firmware (`Software/controller-firmware/`)

All ten modules follow one disciplined pattern: a `.h` states *what facts or decisions this module owns and nothing else*, the `.cpp` implements it, and nearly every non-trivial line has a comment explaining *why*, not *what*.

| File | Lines | Role | Assessment |
|---|---|---|---|
| `config.h` | 147 | Every GPIO pin and tuning constant | Complete, cross-checked against `docs/hardware/esp32_devkit_38pin_pinout.md` — identical GPIO numbers |
| `joystick.cpp/.h` | 111/34 | ADC read, oversampling, calibration, deadzone-with-rescale, sign inversion | Complete, bench-calibrated (`INVERT_X` flipped 30 Aug 2026 per a dated comment) |
| `buttons.cpp/.h` | 111/52 | Debounce, press/release/long-hold edges for ARM/SPEED/CAPTURE/STICK | Complete; seeds state from the physical pin at boot |
| `battery.cpp/.h` | 115/59 | Divider read, EMA filter, 3-level hysteresis, a simulation mode for bench testing | Complete and thoughtful |
| `control.cpp/.h` | 123/42 | Arm/disarm w/ 2s hold, refuses to arm on flat cell or off-centre stick, disarm never gated, CAPTURE/ZERO event latches | Complete for stage 4's scope; safety ordering explicit and sound |
| `leds.cpp/.h` | 91/54 | Mode-driven LED animation, ~900ms power-on self test | Complete |
| `ui.cpp/.h` | 191/32 | Boot banner, serial commands, 10Hz status line, sole owner of LED-colour meaning | Complete; two stale internal comments (§10) |
| `link.cpp/.h` | 103/61 | Stage 4: WiFi STA + ESP-NOW bring-up, unicast peer, 50Hz `ControlPacket` send, timeout-based `up()` | Functionally complete for a one-way link; one real design gap (§11) |
| `protocol.h` | 102 | Wire format (`ControlPacket`), byte-identical to `Software/shared/protocol.h` and the robot-side copy | Complete design (packed struct, `static_assert`, CRC-8) but **three literal copies**, not a symlink (§10) |
| `relay.cpp/.h` | 5/13 | Self-declared dead code: "DEPRECATED... nothing here is referenced" | Verified true by `grep -rn relay Software/` |
| `controller-firmware.ino` | 147 | `setup()`/`loop()` wiring all 8 active modules in a fixed, explained order | Complete; ordering rationale internally consistent with how modules actually read each other |

### 5.2 New/adjacent untracked files (controller-firmware's stage-4 changeset)

- **`Software/shared/protocol.h`** — intended canonical copy; symlink not yet made (three hand-copies, currently byte-identical, confirmed via `diff`).
- **`Software/s3-uart-bridge/s3-uart-bridge.ino`** — explicitly temporary; forwards controller serial output through the S3's USB for one-cable bring-up. Real safety warning about USB/wire bus contention. Cites nonexistent paths (`claude/controller_plan.md`, `claude/esp32_devkit_38pin_pinout.md` — real paths are under `docs/`).
- **`Software/tests/Arduino/robot-stage4-espnow-receiver/`** — robot-side stage-4 exit-test sketch; honest about being bring-up-only ("drives nothing"); correctly verifies CRC/magic before trusting a packet.

### 5.3 `Software/sar/` — victim-detection ML work (new, untracked, added mid-audit)

**`test1-modelRunTime/`** — a genuinely completed benchmark study, not a stub:
- **Dataset & training** (described in `results/BENCHMARK_REPORT.md`, pipeline scripts referenced but *not present in this repo* — `dataset/process_data.py`, `train.py`, `quantize.py`, `benchmark.py` live elsewhere): 574 curated images (248 victim / 326 not-victim), split 458/58/58, two backbones (a custom Tiny CNN and MobileNetV2 α=0.35) trained at 6 resolutions each = 12 models, all quantized to full-int8.
- **Host-side benchmark**: real accuracy/latency/size numbers for all 12 (`results/results.csv`, `BENCHMARK_REPORT.md`), best model `mobilenetv3_96`/`_128` at 87.7% test accuracy.
- **On-device benchmark** (`BENCH_IDF_PORT_PLAN.md`, `results/DEVICE_BENCHMARK_REPORT.md`): all 12 models actually flashed and timed on **the project's own Freenove ESP32-S3 N16R8 board**, on **two separate firmware stacks** (bare ESP-IDF and Arduino core), dated 2026-08-31/09-01. This is real, methodical work: it documents and fixes a task-watchdog bug found mid-sweep, quantifies an IDF-vs-Arduino latency gap (~9.2% mean, IDF faster), finds the static arena-size estimator undershoots real usage by ~50% on average, and lands on a concrete recommendation (`mobilenetv3_96`, ~170–198ms/inference, fits PSRAM comfortably on both stacks).
- **`GUIDE.md` / `README_OLD.md`**: written as a hand-off kit ("Friend Guide... your job is benchmark only") — see the open question in §21 about who actually ran this and on which physical unit, since the results clearly say "Freenove N16R8" (the project's own board) while the prose addresses a "friend" with different candidate boards (DevKitC-1/XIAO).
- **Assessment: genuinely COMPLETE** for its stated scope (pick a model). This is the strongest, most rigorously-evidenced piece of engineering found anywhere in this repository, controller included.

**`test2-initialModelTest/`** — applies the chosen model to a live camera, but is a step behind test1:
- `esp32_camera_victim.ino` (Arduino) and `esp32_camera_victim_idf/` (ESP-IDF, targeting the Freenove board directly) implement the full intended per-frame pipeline: camera capture (RGB565) → letterbox → MobileNet preprocess (`p/127.5 - 1.0`) → int8 quantize → `Invoke()` → dequant → threshold (default 0.65) → act.
- On a positive detection, the Arduino sketch currently **saves only a text marker** (`/victim_N.txt` with the score, not a real photograph) — the file's own comment says a full JPEG capture "needs deinit/reinit; see guide," i.e. it's a known, named gap, not a silent one.
- **No WiFi/HTTP code, no team-tag overlay, no encoder/distance pairing anywhere in this file** — it is a pure camera→score loop.
- `IMPLEMENTATION_GUIDE.md`'s own "Test checklist (send results back)" has every item unchecked in the doc — there's no evidence anywhere in this repo that this sketch has actually been flashed and produced a `VICTIM`/`healthy` result on a real camera.
- **A real, evidenced correctness risk** (detailed in §11): this sketch `#include`s `tensorflow/lite/micro/all_ops_resolver.h` and uses `tflite::AllOpsResolver` + a `MicroErrorReporter` constructor argument, and its own header tells the user to install the `TensorFlowLite_ESP32` Arduino library — the exact combination that `test1-modelRunTime/BENCH_IDF_PORT_PLAN.md` (written as part of the *same* SAR effort) documents as broken: `all_ops_resolver.h` "the vendored esp-tflite-micro 1.4.0 no longer ships" it, `TensorFlowLite_ESP32` "is abandoned at v1.0.0 (2021) and does not compile against core 3.x," and the `MicroErrorReporter` constructor argument was "removed" in the interpreter API actually present. `test1`'s own benchmark sketch was corrected to the newer API; that correction was never carried over to `test2`'s camera sketch.
- **Assessment: PARTIALLY COMPLETE**, and likely **will not compile as-is** against the toolchain the rest of this repo's SAR work has already standardized on.

### 5.4 Bring-up / experimental sketches (`Software/tests/Arduino/`)

| Sketch | Target board | Status (from evidence) |
|---|---|---|
| `controller-stage1-joystick` | classic ESP32 (controller) | **Superseded** — folded into `controller-firmware/joystick.*` |
| `controller-stage2-buttons` | classic ESP32 (controller) | **Superseded** — folded into `controller-firmware/buttons.*`/`control.*` |
| `button-state` | ESP32-S3 | **Standalone reference/experiment**, wrong board family for the controller build |
| `dual_joystick_s3` | ESP32-S3 | **Explicitly kept-for-reference**, per its own header, never carried forward |
| `esp32-specs-check` | classic ESP32 | **Active diagnostic tool** — found the byte-order MAC bug |
| `board_health_check` | ESP32-S3 | **Active diagnostic tool** — source of the verified S3 spec sheet |
| `bluetooth-scanner` | ESP32-S3 | **Experimental / orphaned** — no consumer in the current ESP-NOW architecture |
| `esp32-s3-bluetooth` | ESP32-S3 | **Experimental / orphaned**, no header comment at all — same residue as above |
| `Sketch_04.1_SDMMC_Test` | ESP32-S3 | **Vendor tutorial sketch**, unmodified naming (deliberate, per README) |
| `Sketch_07.1_CameraWebServer` | ESP32-S3 | **Vendor tutorial sketch**, WiFi creds moved to gitignored `arduino_secrets.h` (commit `addff10`); verified via `git log -p` that no real credential ever entered git history |

### 5.5 Documentation (`docs/`)

| File | Assessment |
|---|---|
| `README.md` (root) | Accurate against current code except two omissions (§10) — no mention of `Software/sar/` (added after the README was last touched, so not a real gap yet) or `Software/s3-uart-bridge/` |
| `docs/controller_plan.md` | Most detailed doc in the repo — power-chain rationale, pin map, threshold rationale, dated stage-progress table, explicit "still open" list |
| `docs/hardware/esp32_devkit_38pin_pinout.md` | Complete, cross-checked GPIO reference; matches `config.h` exactly |
| `docs/hardware/esp32_motion_board_log.md` | Bench-verification log, includes a transparent self-correction (byte-reversed MAC) |
| `docs/hardware/esp32s3_spec_sheet.md` | Bench-verification log, two dated corrections (flash bus mode, IDF version string) |
| `docs/competition/SAR_requirements_extract.md` | Careful extraction flagging **7 internal contradictions in the competition brief itself**. No equivalent extraction exists for Swiftplay or Design. |

### 5.6 Tools (`Software/tools/ds4-monitor/`)

`ds4-monitor.c` (379 lines) — Linux/SDL2 host-side gamepad reader, unrelated to the current (custom joystick, no gamepad) controller architecture. The compiled binary on disk predates the last edit to its own source (binary 14 Aug, source 26 Aug) — stale relative to source. Gitignored; the `.gitignore` itself says to rebuild rather than commit it.

## 6. What Has Been Completed

- **Controller hardware bring-up, stages 0–2**: power chain soldered, boots on battery, charges; joystick/buttons ported and bench-tested (dated fix: `INVERT_X` flipped 30 Aug after real bench testing).
- **Controller modular refactor**: explicitly marked complete in the plan doc; matches the current 10-module structure.
- **Stage 3a/3b (LEDs, battery, arm interlock)**: implemented in code; the plan doc itself says the bench-test procedure is written but not yet run.
- **Bench verification of all three MCU boards**: MACs, flash, PSRAM, heap, ESP-IDF/core versions measured and cross-checked, including two self-corrected errors.
- **Stage 4 (ESP-NOW link), in code only**: internally consistent, wired into `setup()`/`loop()`. **Not yet compiled, flashed, or bench-tested** — stated in three independent places (README, plan doc, the `.ino`'s own header).
- **Victim-detection model selection (`Software/sar/test1-modelRunTime/`)**: a real, methodologically sound study — dataset, training, quantization, host benchmark, and **genuine on-device benchmark results on the project's own hardware, on two firmware stacks** — that arrived at a specific, evidence-backed deployment choice (`mobilenetv3_96`, threshold 0.65). This is completed work, not a plan.

## 7. What Is Partially Complete

- **Stage 4 ESP-NOW link** — code-complete, zero hardware validation; `protocol.h` symlink conversion not done.
- **Stage 3a/3b bench test procedures** — written in detail, not yet executed per the plan doc's own tracking.
- **Camera→inference integration (`Software/sar/test2-initialModelTest/`)** — the intended per-frame algorithm is fully written and appears logically correct (letterbox, preprocessing, quantization, and dequantization all match the training-side conventions documented alongside it), but (a) has no evidence in this repo of having actually run on a camera, and (b) is written against a TFLite Micro API combination the project's own adjacent work found broken on the current toolchain (§11) — so "partially complete" here likely means "needs a real fix before it can run," not just "needs a bench test."
- **SAR requirements analysis** — thorough for the document's own content, not yet translated into a task list or connected to any of the above code.

## 8. What Has Not Been Done

**Planned but unimplemented (named explicitly in the project's own docs):**
- Stage 5 (telemetry back) and stage 6 (failsafe) on the controller — fully specified, zero code.
- Robot-side GPIO pin map for the TB6612FNG driver and encoders — explicitly open in two docs.
- The robot↔S3 UART link — "decided: UART... pins and framing not yet specified."
- The onboard heartbeat LED (GPIO2) described in `controller_plan.md`'s pin map — not present in `config.h`/`leds.cpp`, and neither of its two named fallbacks was implemented either.
- `protocol.h`'s symlink conversion — exact commands already written down, not run.

**Not mentioned as "planned" anywhere in this repo — status genuinely unknown from repo evidence alone:**
- Any robot drive/motor-control or encoder firmware.
- Real-photo capture on victim detection (currently a text marker only), team-tag overlay, HTTP upload with retry, and pairing a detection with an encoder-derived distance — none of these exist even as a stub, despite being explicit, mandatory parts of the SAR requirements extract.
- Any chassis/mechanical design files, CAD, or BOM (Design = 25% of score).
- A Swiftplay-equivalent requirements extract (one exists for SAR; none for Swiftplay).
- Confirmation that `esp32_camera_victim.ino`/`esp32_camera_victim_idf` have ever actually been flashed and produced a working detection on real camera hardware.

## 9. Experimental / Deprecated / Obsolete Components

- **`relay.cpp`/`relay.h`** — self-declared deprecated, verified unreferenced. Safe to delete.
- **`bluetooth-scanner.ino`, `esp32-s3-bluetooth.ino`, `Software/tools/ds4-monitor/`** — residue of an apparently abandoned Bluetooth/gamepad control-input direction, superseded by the custom-joystick + ESP-NOW design. No doc narrates this decision the way the ESP-NOW-vs-Bluetooth *link* decision is narrated elsewhere.
- **`dual_joystick_s3.ino`** — explicitly kept as reference, not obsolete by its own account.
- **`s3-uart-bridge.ino`** — self-declared temporary, with a defined expiry condition.
- **`controller-stage1-joystick.ino` / `controller-stage2-buttons.ino`** — superseded by the modular refactor, kept as historical bench sketches per the project's own convention.
- **`Software/sar/test1-modelRunTime/README_OLD.md`** — explicitly superseded by `GUIDE.md` in the same folder (both describe the same benchmark kit; `README_OLD` is the earlier draft, per its filename and near-identical, less detailed content).

## 10. Discrepancies & Contradictions

| Issue | Evidence | Impact | Recommended resolution |
|---|---|---|---|
| `protocol.h` exists as **3 independent copies**, not a symlink | Byte-identical `diff` across all three; all three files' own headers, plus `controller_plan.md` §6 and the README, call this out as a known, temporary risk | Low today; becomes a silent-corruption risk the moment the packet format changes on only one side | Run the two `ln -s` commands `controller_plan.md` already specifies |
| `s3-uart-bridge.ino` cites doc paths `claude/controller_plan.md` and `claude/esp32_devkit_38pin_pinout.md` | Those paths don't exist; real files are under `docs/` (confirmed via `find`) | Cosmetic (a comment, not an include) | Fix the two path strings |
| `ui.h`'s comment says LED link-state semantics move "at stage 5" | The actual change (green LED → `Link::up()`) happened at **stage 4**, per `ui.cpp`, the `.ino` header, and the plan doc's progress table, all agreeing | Cosmetic, one stage off | Update the comment |
| `ui.h`'s doc-comment for `handleInput()` says "Currently: 'c' recalibrates..." | `dispatch()` also handles `t` and `v`/`v<volts>`, both real and documented elsewhere | Minor — incomplete if read in isolation | List all three commands |
| `controller_plan.md` §3's LED table reads as describing the current design | Actual stage-4 code (green=local send-timeout, yellow=local arm latch) matches the doc's own §5 stage-5 target, not present-day reality | Low — resolves once §5 is read, but §3 alone is ambiguous | Annotate §3's header "(stage 5 target)" |
| README omits `Software/s3-uart-bridge/` (Layout section) and `relay.*` (firmware table) | Confirmed absent from both; `Software/sar/` is also unlisted but postdates the README edit, so not yet a "real" omission | Low — no functional effect, but a new reader can't discover these from the README alone | Add the missing lines; add `Software/sar/` too, next time the README is touched |
| Untracked `relay.cpp`/`relay.h` sit next to the real stage-4 changeset | `git status` shows them alongside `link.cpp/.h`, `protocol.h`, etc. | A broad `git add` on the stage-4 commit sweeps deprecated-on-arrival files into history | Delete both before committing, or commit with an explicit note |
| **`test2-initialModelTest`'s camera sketch uses a TFLite Micro API the project's own `test1-modelRunTime` work found broken on the current toolchain** | `esp32_camera_victim.ino` includes `all_ops_resolver.h`, uses `tflite::AllOpsResolver` and a `MicroErrorReporter` constructor argument, and its header says to install `TensorFlowLite_ESP32`. `BENCH_IDF_PORT_PLAN.md` (same `Software/sar/` effort, same repo) documents that `all_ops_resolver.h` isn't shipped by the vendored esp-tflite-micro 1.4.0, that `TensorFlowLite_ESP32` "does not compile against core 3.x," and that the `MicroErrorReporter` ctor argument was removed from the interpreter constructor actually present | **High** — this sketch is the only code in the repo that turns "camera sees something" into a score, and it is very likely to fail to compile exactly as `test1`'s original sketch did, for a documented reason the fix already exists for | Port `esp32_camera_victim.ino` to the same `MicroMutableOpResolver<7>` + 4-argument `MicroInterpreter` pattern `test1`'s corrected `esp32_benchmark.ino`/`bench/main/main.cpp` already use |
| `GUIDE.md`/`IMPLEMENTATION_GUIDE.md` address a "friend" testing on a DevKitC-1/XIAO board, but the actual on-device results are recorded against the project's own Freenove N16R8 | `results/DEVICE_BENCHMARK_REPORT.md` states the target hardware as "the physical board both sweeps ran on: Freenove ESP32-S3 WROOM N16R8" | Not a functional problem, but leaves ambiguous who ran the benchmarks and on what hardware — relevant to trusting the numbers for the project's actual board | Clarify in the doc which board(s) the numbers actually came from, if this is shared further |

## 11. Technical Problems

**Confirmed:**
1. **`test2-initialModelTest/sketches/esp32_camera_victim/esp32_camera_victim.ino` is written against a library/API combination the project's own adjacent work documents as broken on the toolchain in use** (detailed in §10). This is the single highest-value fix identified in this audit — a same-repo, same-author precedent for the exact correction already exists.
2. **`ds4-monitor` binary is stale relative to its source** (14 Aug binary vs. 26 Aug source) — gitignored, low stakes, but concretely verified.
3. **`relay.h`/`relay.cpp`'s self-declared "nothing references this" claim is correct** — verified by grep, not taken on faith.

**Plausible / requires hardware verification (cannot be fully confirmed from static reading alone):**
1. **`Link::update()`'s CAPTURE/ZERO one-shot events may be lost on a failed send, silently.** `s_pendingCapture`/`s_pendingZero` are cleared unconditionally right after calling `esp_now_send()` (an async call whose real success/failure is only known later, in `onSent()`). A CAPTURE press sent in a packet that ESP-NOW fails to deliver is dropped with no recovery path. Not yet harmful (nothing on the robot side consumes CAPTURE), but a design gap worth closing before robot-side SAR logic is built on top of this link.
2. **Controller stage 4 is entirely unverified against real silicon.** No compiled binary, build log, or CI exists. One positive, checkable sign: the receiver sketch's `onReceive()` signature matches the ESP-NOW API shape for the IDF/core versions actually installed, per `esp32_motion_board_log.md`.
3. **`esp32_camera_victim.ino`'s "on victim, save a photo" path is a documented stub** (a text marker, not a JPEG) — not a hidden bug, but worth tracking as unfinished rather than assuming it's closer to done than it is.
4. **Bluetooth-input architectural residue** (§9) has no doc explaining why it was abandoned, unlike the ESP-NOW-vs-Bluetooth *link* decision, which is narrated.
5. **`arduino_secrets.h` contains live personal WiFi credentials in plaintext on disk.** Correctly gitignored; `git log -p` confirms no real credential ever entered git history (redacted from the first commit that touched the file). Not a repo-security defect, but a plaintext-secret-on-disk fact worth the user's awareness independent of git hygiene.

## 12. Dependency & Data Flow Analysis

**Controller firmware, internal (fully traced):**
```
Joystick::update() ─┐
Buttons::update()   ├──▶ Control::update() ──▶ Link::update() ──▶ (ESP-NOW radio, async)
Battery::update()  ─┘         │                      │
                               │                      └──▶ UI::update() reads Link::up()/counts
                               └──────────────────────────▶ UI::update() reads Control's state
Leds::update() ◀── UI::updateIndicators() (sole owner of LED meaning)
```

**Cross-board (controller → robot), traced as far as code exists:**
```
Control's decision → Link builds+signs Protocol::ControlPacket → esp_now_send()
   → [2.4GHz ESP-NOW, unicast, channel 1, no encryption]
   → robot-stage4-espnow-receiver: verify length/magic/CRC, track seq loss
   → Serial.printf() only — DEAD END BY DESIGN (bring-up sketch, drives nothing)
```

**Victim-detection pipeline, traced as far as code exists:**
```
574-image dataset (outside this repo) → train/quantize (outside this repo)
   → 12 .tflite/.h model pairs (IN this repo, Software/sar/*/models/)
   → on-device benchmark (bench/main.cpp, esp32_benchmark.ino) → mobilenetv3_96 selected
   → esp32_camera_victim.ino: camera(RGB565) → letterbox → preprocess → quantize
        → Invoke() → dequant → threshold → [victim: write text marker to SD]
   → DEAD END — no team-tag overlay, no encoder distance, no HTTP upload, nothing consumes
     the SD marker or the serial score line
```
No code anywhere in this repository connects the victim-detection dead end to the controller/robot dead end, or either to a wheel-encoder reading that does not yet exist.

## 13. Intended vs Current State

| Intended (per docs/requirements) | Current state | Blocking? |
|---|---|---|
| Controller stages 0–6 complete and bench-tested | Stages 0–3b done+tested; stage 4 written, unverified; stages 5–6 not started | Blocks a safe field test of the controller alone |
| Robot receives control packets and drives motors | Robot receives+prints only; no motor/driver code | **Yes** — hard blocker for any physical movement |
| Robot reports telemetry back | Not started | Blocks stage 5/6 and `controller_plan.md` §3's LED design |
| Robot has wheel encoders, computes distance | No encoder code or pin assignment | **Yes** — hard blocker for Search & Rescue |
| Camera detects victims on-device | **Model chosen and on-device benchmarked; live-camera sketch written but unconfirmed and likely non-compiling as-is (§11)** | Was a hard blocker; now a **near-miss** — one documented API fix plus a real bench test away from working |
| Detection paired with encoder distance, tagged, uploaded with retry | None of this exists in any form | **Yes** — hard blocker for the majority of SAR's scoring criteria, independent of whether detection itself works |
| Failsafe: robot stops on link loss | Specified in detail, not implemented | Hard blocker for any *safe* physical test once motors exist |
| Chassis/mechanical design (25% of score) | No files of any kind in this repo | Unknown/out of scope for this repo — see §21 |

## 14. Current Project Status

- 🟡 **Ground controller firmware** — input/policy/LED/battery layers complete and consistent; radio layer code-complete but unverified on hardware.
- ⚪ **Ground controller bench validation (stage 4)** — not run, per the project's own docs.
- 🔴 **Robot motion firmware** — does not exist anywhere in this repo.
- 🟢 **Victim-detection model selection** — genuinely complete, rigorously benchmarked on real hardware, clear winning choice.
- 🟡 **Camera → live inference** — fully designed and written, but unconfirmed and carrying a specific, well-evidenced likely compile blocker.
- 🔴 **SAR data pipeline (tag, pair with distance, upload, retry)** — does not exist in any form.
- 🟢 **Hardware characterization (all 3 boards)** — complete, cross-verified, self-corrected twice.
- 🟢 **Competition requirements analysis (SAR only)** — complete and unusually rigorous.
- ⚪ **Competition requirements analysis (Swiftplay, Design)** — not present in this repo.
- 🟠 **Documentation-vs-code synchronization** — mostly good; several small cosmetic drifts (§10), one significant one (test2 vs. test1's own findings).

**Overall maturity:** still not a single number, and the `Software/sar/` discovery makes the picture more uneven, not more even: one subsystem (controller) is disciplined and well along; one (victim-detection model choice) is genuinely finished and rigorous; one (camera integration) is close but has a concrete, named defect standing between it and a first real test; and the pieces that turn any of this into a scoring competition run — robot motion, encoders, SAR data packaging/upload, and all mechanical work — remain entirely absent from this repository.

## 15. Blockers

**Hard blockers:**
1. Robot drive/motor firmware + TB6612FNG pin map — nothing exists.
2. Wheel encoder firmware — nothing exists; blocks distance-tagging for SAR entirely.
3. SAR data packaging (team tag, real photo capture, encoder-distance pairing) and HTTP upload+retry — nothing exists, independent of whether detection itself works.
4. Bench-testing controller stage 4 on real hardware — written, never run.
5. Fixing `esp32_camera_victim.ino`'s TFLite Micro API mismatch (§11) before it can be flashed and tested at all.

**Soft blockers:**
1. `protocol.h`'s three-copy-not-symlink situation.
2. Stage 5/6 (telemetry, failsafe).
3. Confirming the camera sketch's letterbox/preprocess/quantize logic against a real, known-labeled image once it compiles (it has never been run, so its *logical* correctness — not just its compile status — is also unverified).
4. The small doc/comment drifts in §10.

**Risks:**
1. **Calendar risk, still the largest**: brief deadlines already passed; main event ~3–4 weeks out; the pieces that connect victim-detection to a scoring submission (encoder pairing, upload, retry, mechanical) don't exist yet.
2. Robot-side GPIO pin map undecided — blocks encoder/motor design from starting.
3. CAPTURE/ZERO drop-on-failed-send gap (§11) — low risk today, real risk once robot-side SAR logic trusts that signal.
4. Bluetooth-stack flash-size risk if legacy Bluetooth code is ever reactivated instead of deleted.
5. The device-benchmark numbers in `test1-modelRunTime` are trustworthy for the Freenove N16R8 specifically; if the eventual robot camera board differs, they should be re-confirmed, not assumed to transfer.

## 16. Risks

(Repeated per the requested structure; content matches §15's "Risks" — not duplicated further here.)

## 17. Cleanup Candidates

**Not acted on — for the user's review only:**
1. `Software/controller-firmware/relay.cpp` / `relay.h` — dead code, verified unreferenced.
2. `Software/tools/ds4-monitor/ds4-monitor` (compiled binary) — stale vs. its own source.
3. `Software/tests/Arduino/bluetooth-scanner/`, `Software/tests/Arduino/esp32-s3-bluetooth/`, `Software/tools/ds4-monitor/ds4-monitor.c` — candidates for deletion or a one-line "why kept" note.
4. Stale `claude/...` path references in `Software/s3-uart-bridge/s3-uart-bridge.ino`'s header.
5. `ui.h`'s two stale comments (§10).
6. README's `Layout` section and firmware table — missing `Software/s3-uart-bridge/`, `Software/sar/`, and `relay.*`.
7. `docs/controller_plan.md` §3's LED table — one "(stage 5 target)" annotation.
8. `Software/sar/test1-modelRunTime/README_OLD.md` — superseded by `GUIDE.md` in the same folder; candidate for deletion once confirmed no longer referenced.

## 18. What We Should Do Next

**Immediate (1–3 actions):**
1. **Fix `esp32_camera_victim.ino`'s TFLite Micro API** using the exact pattern already proven in `Software/sar/test1-modelRunTime/` (`MicroMutableOpResolver<7>`, 4-argument `MicroInterpreter`, drop `all_ops_resolver.h`/`MicroErrorReporter`/`TensorFlowLite_ESP32`). *Why:* this is the one concrete, named defect standing between "victim detection is designed" and "victim detection has been tested once." *Files:* `Software/sar/test2-initialModelTest/sketches/esp32_camera_victim/esp32_camera_victim.ino` (and the `_idf` variant, which appears to already follow the corrected pattern per its own doc — verify). *Verify:* it compiles under the same Arduino core 3.x used elsewhere in this repo.
2. **Bench-test controller stage 4** on real hardware. *Why:* the one piece of finished-looking controller code that has never met real silicon. *Files:* `Software/controller-firmware/*`, `Software/tests/Arduino/robot-stage4-espnow-receiver/*`. *Verify:* the exit test already written in `controller_plan.md`'s stage-4 row.
3. **Decide the robot-side GPIO pin map** for the TB6612FNG + encoders, respecting the ADC1-only constraint already documented for this board family. *Why:* the single most-repeated "still open" item across three docs; nothing robot-side can start without it.

**Short-term:**
- Once (1) above compiles, run the actual camera test the `IMPLEMENTATION_GUIDE.md` checklist already specifies (point at printed victim/healthy images, confirm scores separate around 0.65).
- Convert `protocol.h`'s three copies to symlinks; commit the stage-4 changeset (after resolving `relay.*`).
- Start robot-side firmware once the pin map is decided: motor driver control, then encoders.

**Medium-term:**
- Build the actual SAR data-packaging path: real-photo capture on victim (not a text marker), team-tag overlay, pairing with encoder distance, HTTP POST with retry — none of which exist yet even as a stub, and all of which are mandatory per the requirements extract.
- Stage 5 (telemetry) and stage 6 (failsafe) on the controller.

**Finalization:**
- End-to-end field test: controller → robot → motors → encoders → camera → detection → tagged/paired upload → server, with the stage-6 failsafe verified by cutting controller power mid-drive.
- A Swiftplay-equivalent requirements extract.
- Whatever mechanical/CAD/BOM work the Design category needs — outside this repo's current scope.

## 19. Dependency-Aware Roadmap

```
[done] Controller stages 0-3b
   │
   ▼
[written, UNVERIFIED] Controller stage 4  ──▶  BENCH TEST
   │                                                │
   │                                                ▼
   │                                  protocol.h → symlinks
   ▼
Robot pin map decided ──▶ Motor/driver firmware ──▶ Encoder firmware
                                                          │
[done] Victim-detection model chosen (mobilenetv3_96)     │
   │                                                       │
   ▼                                                       │
Fix esp32_camera_victim.ino's TFLite API  ──▶  Real camera test        │
   │                                                       │            │
   ▼                                                       ▼            │
Real-photo capture + team-tag overlay ──▶ Pair detection with encoder distance
   │                                                                    │
   ▼                                                                    │
HTTP upload + retry ◀───────────────────────────────────────────────────┘
   │
   ▼
Controller stage 5 (telemetry) ──▶ stage 6 (failsafe)
   │
   ▼
End-to-end field test with failsafe verification
```
The victim-detection fix/test track and the controller/robot-motion track are independent and can run in parallel; they converge only at "pair detection with encoder distance," which needs both a working camera pipeline and a working encoder reading.

## 20. Verification Plan

- **`esp32_camera_victim.ino` fix**: compiles under Arduino core 3.x with the corrected resolver/interpreter API; boots and prints the same log shape already specified in `IMPLEMENTATION_GUIDE.md`.
- **Camera test**: point at 5 known victim + 5 known healthy prints, confirm scores separate cleanly around the 0.65 threshold, per the guide's own tuning procedure.
- **Controller stage 4 bench test**: `LINK up`, non-zero `TX`, `FAIL` at 0 on the controller; robot sketch's `rx` increments with `rej=0 lost=0` normally, and correctly counts `rej`/`lost` when the link is deliberately interrupted.
- **Stage 3a/3b bench test**: the exact voltage sweep already written in `controller_plan.md`, confirming thresholds and hysteresis.
- **Robot pin map**: cross-checked against the same trap list (`esp32_devkit_38pin_pinout.md`) already used for the controller.
- **`protocol.h` symlink conversion**: `ls -la` shows symlinks; both sketches still compile unchanged.
- **Robot motor/encoder firmware** (once written): bench test with wheels off the ground, confirming direction/PWM sense and encoder tick counts against a known manual rotation.
- **SAR data path** (once written): a detection produces a real JPEG, a correct team tag, a plausible paired distance, and a successful (or correctly retried) POST to a mock server.
- **End-to-end field test**: cut controller power mid-drive, confirm the robot stops immediately, disarms after 2s, doesn't lurch on reconnect.

## 21. Questions / Unknowns

1. **Who actually ran the `Software/sar/test1-modelRunTime` on-device benchmarks, and on which physical board?** The kit's prose ("Friend Guide," candidate DevKitC-1/XIAO boards) doesn't match `DEVICE_BENCHMARK_REPORT.md`'s statement that both sweeps ran on "the physical board" — described as the project's own Freenove N16R8. This matters for how much to trust the numbers for the actual competition hardware.
2. **Has `esp32_camera_victim.ino` (or its IDF sibling) ever actually been flashed and run against a real camera by anyone?** No result, log excerpt, or filled-in checklist exists in this repo to confirm it.
3. **Does robot motor/encoder/upload firmware exist outside this repository** (another repo, another branch, a teammate's machine, work not yet committed)? This repo alone cannot distinguish "not started" from "developed elsewhere."
4. **Has the Mock Event / Video Submission deliverable (due 17 Aug 2026) already been produced**, using capabilities not reflected in this repo?
5. **Is there any mechanical/CAD/BOM work for the Design category (25% of score)** anywhere accessible to the user? None exists in this repository.
6. Where does the `Software/sar/` dataset and training pipeline (`dataset/process_data.py`, `train.py`, `quantize.py`, `benchmark.py`, referenced but absent here) actually live? Should it be brought into this repo for reproducibility, or is it intentionally kept elsewhere?
7. Is the robot pack chemistry/cell-count decided anywhere outside this repo? `controller_plan.md` §6 lists it as open.
8. Whether the CAPTURE/ZERO drop-on-failed-send behavior (§11) is an accepted risk or an oversight — only the team can say whether SAR-trigger reliability at this level matters given ESP-NOW's typically low loss rate at short range.
