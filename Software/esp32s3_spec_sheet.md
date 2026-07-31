# Freenove ESP32-S3-WROOM CAM Board — Verified Spec Sheet

**Board variant:** N16R8 (16 MB flash / 8 MB octal PSRAM)
**Verified on:** 21 July 2026, via `board_health_check.ino`
**Status:** ALL CHECKS PASSED — cleared for camera bring-up

All figures below are *measured on the actual unit*, not copied from marketing
material. Where a vendor figure differs from the measurement, the measurement wins.

---

## 1. Identity

| Property | Value |
|---|---|
| Chip model | ESP32-S3 |
| Silicon revision | v0.2 |
| Package | QFN56 |
| MAC address (WiFi STA) | `D0:CF:13:00:2C:E0` |
| Crystal | 40 MHz |

The MAC is the board's permanent unique ID. Useful as a fallback device
identifier if we ever run two boards side by side during testing.

---

## 2. Processor

| Property | Value |
|---|---|
| Cores | 2 (plus a separate LP core, unused by Arduino) |
| Clock | 240 MHz |
| Internal SRAM | 512 KB |

**Project relevance:** two cores is the architectural gift. Camera capture and
JPEG work get pinned to one core; WiFi, HTTP and the upload retry queue get the
other. An upload that stalls or retries cannot delay the next capture.

---

## 3. Memory — measured

### Flash
| Property | Value |
|---|---|
| Total | 16 MB |
| Bus speed | 80 MHz, QIO mode |
| Partition scheme | 16M Flash (3 MB APP / 9.9 MB FATFS) |
| App partition free | 3072 KB |

Only ~9% of the app partition was used by a full diagnostic sketch, so there is
ample room for the camera driver, TLS, JSON and an HTTP client together.

### Internal heap
| Property | Value |
|---|---|
| Free heap at boot | 358,824 bytes (~350 KB) |
| Minimum free ever | 353,400 bytes |
| Largest free block | 303,092 bytes (~296 KB) |

### PSRAM — the critical one
| Property | Value |
|---|---|
| Detected | Yes |
| Type | Octal SPI (OPI), in-package, AP_3v3 |
| Total | 8 MB (8192 KB) |
| Free at boot | 8188 KB |
| **Largest contiguous block** | **8063 KB** |
| Integrity test (64 KB write/read/verify) | **PASS** |

**Why contiguous matters:** camera frame buffers must be allocated as one
unbroken block. Plenty of total free PSRAM with a small largest block is the
fragmentation failure that makes `esp_camera_init()` fail in a way that looks
like a wiring fault. At boot we have 8063 KB contiguous — no constraint.

**Why the integrity test matters:** `psramFound()` only proves the chip was
detected. Writing a pattern and reading it back proves the octal bus is
timing-clean at 240 MHz. It passed, so the OPI configuration is genuinely correct.

---

## 4. Camera

| Property | Value |
|---|---|
| Sensor | OV2640 |
| Max resolution | 2 MP, UXGA 1600×1200 |
| Interface | Parallel DVP, ribbon connector |
| Pin map | **To be confirmed against Freenove docs for this board revision** |

**Headroom calculation for the competition:** the required upload is QVGA JPEG
under 150 KB. Against 8 MB of contiguous PSRAM that is trivial — a QVGA JPEG at
~100 KB plus its base64 expansion (~135 KB) uses under 3% of available PSRAM.

This means we can afford to **capture at a higher resolution and downscale**,
which generally produces a sharper result than capturing natively small. Given
that a blurry image scores zero, that headroom is worth spending.

Double buffering (`fb_count = 2`) is affordable and lets capture overlap with
upload.

---

## 5. Wireless

| Property | Value |
|---|---|
| WiFi | 2.4 GHz 802.11 b/g/n |
| Bluetooth | BLE 5 |
| Bluetooth Classic | **Not supported** (S3 is BLE-only) |

**Design note:** this board carries all WiFi duty — image upload, NTP time sync,
and the HTTP retry queue. Ground control over Bluetooth deliberately lives on the
*other* MCU (classic ESP32), so that the robot can still be armed and driven if
this board or the venue WiFi fails.

---

## 6. Storage and I/O

| Property | Value |
|---|---|
| microSD | SDMMC slot, 1 GB card supplied |
| USB | Two USB-C ports: native USB-OTG, plus onboard UART uploader |
| Status LED | Programmable RGB |

**microSD role in the project:** satisfies the competition requirement to queue
failed uploads to storage rather than losing them. Doubles as a flight recorder —
saving every frame with its distance stamp makes post-run review of framing and
focus possible without re-running the robot.

**USB note:** the current build reports `USB CDC On Boot: 0`, meaning we are
flashing and monitoring through the **UART bridge port**. This works fine. If
switching to the native USB port, `USB CDC On Boot` must be set to Enabled or
the serial monitor will stay silent.

---

## 7. Verified toolchain configuration

These are the exact settings that produced the passing result. Treat as the
known-good baseline.

| Arduino IDE setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| **PSRAM** | **OPI PSRAM** ← critical; wrong value breaks the camera |
| **Flash Size** | **16MB (128Mb)** ← critical for N16R8 |
| Flash Mode | QIO 80 MHz |
| Partition Scheme | 16M Flash (3 MB APP / 9.9 MB FATFS) |
| CPU Frequency | 240 MHz (WiFi) |
| Arduino Runs On | Core 1 |
| Events Run On | Core 1 |
| Upload Speed | 921600 |
| USB CDC On Boot | Disabled (using UART bridge port) |

Core versions in use: **ESP-IDF v5.5.4**, Arduino-ESP32 core **3.3.10**,
esptool **v5.3.0**.

---
