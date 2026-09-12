# Quickstart — Victim Detection (MobileNet96 default, 0.65)

1. Copy model header:
   ```powershell
   copy models\mobilenetv3_96\model_96_int8.h sketches\esp32_camera_victim\model.h
   ```
2. Check `sketches/esp32_camera_victim/config_model.h` — defaults are already:
   `MODEL_W 96`, `MODEL_H 96`, `THRESHOLD 0.65`, `CAM_BOARD 0` (XIAO; set 1 for DevKitC-1).
3. Arduino IDE: ESP32 core 3.0.5+, lib `TensorFlowLite_ESP32`, board `ESP32S3 Dev Module` / `XIAO_ESP32S3`, Flash 8MB, Partition 8M OTA, **PSRAM Enabled (OPI)**, CPU 240MHz, USB CDC On Boot Enabled.
4. Upload `esp32_camera_victim.ino`, Serial 115200. Expect:
   `Model: mobilenetv3_96 96x96 THRESHOLD=0.65`, `Arena used: ~219000`, `score=... VICTIM/healthy`.
5. `score > 0.65` = victim. Tune 0.5-0.7 with 10 test prints if needed.
6. 128 optional: copy `models\mobilenetv3_128\model_128_int8.h` to `model.h`, switch config block, re-flash (~280ms).

Full details: `IMPLEMENTATION_GUIDE.md`.

## ESP-IDF variant (Freenove ESP32-S3 WROOM, OV3660)

A separate, IDF-native firmware for the Freenove ESP32-S3 WROOM (N16R8) + OV3660
camera lives at `sketches/esp32_camera_victim_idf/` (not the XIAO/DevKitC-1
boards above — pins are fixed for Freenove). Build:
```
esp-idf        # activates the idf.py venv
cd sketches/esp32_camera_victim_idf
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```
Model/threshold are edited the same way, in that project's `main/config_model.h`.
SD card uses fixed SDMMC pins (CLK=39, CMD=38, D0=40) per Freenove's own board
reference — SD save is optional/non-fatal if wiring differs.
