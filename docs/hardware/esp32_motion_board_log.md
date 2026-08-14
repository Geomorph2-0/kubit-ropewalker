# Classic ESP32 Pair — Controller + Robot (verified)

**Logged:** 13 August 2026
**Status:** both boards fully characterised. Nothing outstanding.
**Role:** the two classic ESP32s. One is the ground controller, one rides the
robot. Both are the *other* MCU referenced in [`esp32s3_spec_sheet.md`](esp32s3_spec_sheet.md) §5 — the
S3 CAM board keeps camera + WiFi upload duty.

**Boards:** 38-pin ESP32 DevKit, micro-USB, **CP2102** USB-UART bridge.
Read out via [`esp32-specs-check.ino`](../../Software/tests/Arduino/esp32-specs-check/) on both units.

## Hardware — measured independently on each board, identical

| Property | Value |
|---|---|
| Chip model | ESP32-D0WD-V3 |
| Silicon revision | v3.1 |
| Cores / clock | 2 @ 240 MHz |
| Wireless | WiFi b/g/n, BLE, **Bluetooth Classic** |
| Flash | 4 MB, 80 MHz, DIO mode |
| Sketch space | 279 KB used / 1280 KB free |
| Free heap at boot | 335,060 B (~327 KB) |
| Max alloc block | 110,580 B |
| PSRAM | None |
| ESP-IDF (Arduino core) | v5.5.2-729-g87912cd291 |

Both units report these figures **byte-for-byte identically**, including heap
and largest-free-block. Expected for identical silicon running an identical
binary, and a useful baseline: a future divergence in these numbers means
something changed in the build, not in the hardware.

## MAC addresses — both boards, all observed

Same Espressif OUI `F4:2D:C9`, different device portions. Read via
`esp_read_mac()`; no derived or assumed values remain.

| Interface | Controller | Robot |
|---|---|---|
| **Base / WiFi STA** | **`F4:2D:C9:71:7B:0C`** | **`F4:2D:C9:71:0A:7C`** |
| WiFi SoftAP | `F4:2D:C9:71:7B:0D` | `F4:2D:C9:71:0A:7D` |
| Bluetooth | `F4:2D:C9:71:7B:0E` | `F4:2D:C9:71:0A:7E` |
| Ethernet (no PHY) | `F4:2D:C9:71:7B:0F` | `F4:2D:C9:71:0A:7F` |

Use the **WiFi STA** addresses as ESP-NOW peer addresses. Note the two device
portions are not adjacent (`71:7B:0C` vs `71:0A:7C`) — these boards did not
come off the line in sequence, so never assume one address from the other.

> **Correction — an earlier reading of the controller was wrong.** The
> widely-copied `ESP.getEfuseMac()` shift-print idiom emits the MAC
> **byte-reversed**, which gave `0C:7B:71:C9:2D:F4`. That is not this board's
> address. Confirmed four independent ways: `F4:2D:C9` is registered to
> Espressif (2025-09-18) while `0C:7B:71` is not assigned to them at all; the
> SDK increments the *last* octet for derived interfaces; the robot board's
> untouched 2018 factory firmware independently reported an `F4:2D:C9` prefix;
> and the robot's `esp_read_mac()` output then matched that factory reading
> exactly. Anywhere the reversed form was recorded, it is void.
>
> The S3 CAM board's `D0:CF:13:00:2C:E0` in [`esp32s3_spec_sheet.md`](esp32s3_spec_sheet.md) was checked
> against the same registry and **is correct** — `D0:CF:13` is an Espressif
> OUI. No correction needed there.

## Factory firmware found on the robot board (as shipped)

Before flashing, the robot board booted **Espressif ESP-AT** — the stock
AT-command firmware most WROOM-32 dev boards ship with.

- Bin version `1.1.2 (Wroom32)`, built on **ESP-IDF v3.0.3** (2018-era)
- Partition table had no factory app: `otadata`, `ota_0`, `ota_1`, plus the
  `at_customize` partition that identifies ESP-AT
- Booted straight into `sta + softAP` mode and began broadcasting

Harmless and normal — not a sign of a used or tampered board. Overwritten by
the first Arduino upload, which installs a factory app at `0x10000` and
replaces the partition table. Its one lasting value: it gave up the robot
board's MAC before anything was flashed, and later corroborated the byte-order
correction above.

## Notes for pick-up

1. **Bluetooth Classic is present on both** — confirms the design split in the
   S3 spec sheet: ground control lives here, not on the S3, so the robot stays
   drivable if venue WiFi or the camera board fails.
2. **4 MB flash is the constraint.** 1280 KB free app space implies the default
   4 MB partition scheme (~1.2 MB app, with OTA). The BT Classic stack is
   large; if a gamepad library pushes the binary past that, switch to
   **Huge APP (3 MB, No OTA)** rather than trimming features. Check at first
   real build.
3. **Boot logs are clean on both** — `rst:0x1 (POWERON_RESET)`,
   `SPI_FAST_FLASH_BOOT`, DIO @ clock div 1. No brownout, no watchdog reset.

## Toolchain incident (13 Aug 2026)

Arduino IDE 2.x segfaulted during re-upload and restarted to a white window.
Root cause was **not** the board: `dmesg` showed a single clean CP2102
enumeration on `ttyUSB0` with no re-enumeration, while the crash was a stack
overflow inside the IDE's own Electron thread pool (fault address at `sp-8` on
a `push rbx` prologue), alongside system-wide memory pressure.

Mitigations: clear `~/.config/arduino-ide/GPUCache`; **close the Serial Monitor
before uploading** (long-standing IDE 2.x bug); or bypass the IDE for
identity reads with `esptool --port /dev/ttyUSB0 read-mac` / `flash-id`, which
talk to the ROM bootloader and need no compile step.

## Next, when picked up

Neither of these is decided yet:

- **GPIO pin map** for the TB6612FNG driver, encoders and any endstops, avoiding
  the strapping and input-only pins.
- **Controller link**: Bluetooth Classic gamepad vs BLE vs ESP-NOW, and the
  partition-scheme consequence of whichever wins.
