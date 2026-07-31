/*
 * board_health_check.ino
 * -----------------------------------------------------------------------------
 * Hardware sanity check for the Freenove ESP32-S3-WROOM CAM board (N16R8).
 *
 * Keep this sketch. Flash it whenever the board behaves oddly, after changing
 * Arduino IDE settings, or before starting a new subsystem. It verifies the
 * things that silently break camera work later:
 *
 *   - PSRAM present, correct size, AND actually functional (write/read/verify)
 *   - Largest CONTIGUOUS PSRAM block (camera frame buffers need contiguous)
 *   - Flash size matches N16R8 (16 MB)
 *   - Correct byte order MAC address
 *   - Reset reason (brownout vs watchdog vs panic - vital once motors run)
 *
 * Required Tools settings:
 *   Board: ESP32S3 Dev Module | PSRAM: OPI PSRAM | Flash Size: 16MB (128Mb)
 *   USB CDC On Boot: Enabled  | Partition: 16M Flash (3MB APP/9.9MB FATFS)
 *
 * Expected PASS output: PSRAM 8 MB, integrity OK, flash 16 MB, 2 cores.
 * -----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>

// Size of the PSRAM integrity test buffer (64 KB is plenty and fast).
static const size_t PSRAM_TEST_BYTES = 64 * 1024;

// Track overall pass/fail so the sketch ends with a single clear verdict.
static bool g_allChecksPassed = true;

static void fail(const char *what) {
  g_allChecksPassed = false;
  Serial.printf("  !! FAIL: %s\n", what);
}

// -----------------------------------------------------------------------------
// Reset reason: once motors and WiFi share a battery, telling a brownout apart
// from a watchdog timeout from a software panic is the difference between
// debugging power and debugging code.
// -----------------------------------------------------------------------------
static const char *resetReasonToString(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Power-on (normal cold boot)";
    case ESP_RST_EXT:       return "External reset pin";
    case ESP_RST_SW:        return "Software reset (esp_restart)";
    case ESP_RST_PANIC:     return "PANIC / exception  <-- crash, check backtrace";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog  <-- ISR blocked too long";
    case ESP_RST_TASK_WDT:  return "Task watchdog       <-- loop blocked too long";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT            <-- power problem, not code";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "Unknown";
  }
}

// -----------------------------------------------------------------------------
// PSRAM integrity: psramFound() only proves the chip was detected. It does not
// prove the octal bus is timing-clean at 240 MHz. Writing a pattern and reading
// it back does. This catches marginal boards and wrong flash/PSRAM mode combos.
// -----------------------------------------------------------------------------
static bool testPsramIntegrity(size_t bytes) {
  uint32_t *buf = (uint32_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) {
    Serial.printf("  Could not allocate %u bytes in PSRAM\n", (unsigned)bytes);
    return false;
  }

  const size_t words = bytes / sizeof(uint32_t);

  // Pattern mixes the index with a constant so a stuck address line, a stuck
  // data line, or an aliasing fault all produce a mismatch.
  for (size_t i = 0; i < words; i++) {
    buf[i] = (uint32_t)(i * 2654435761u) ^ 0xA5A5A5A5u;
  }

  size_t errors = 0;
  for (size_t i = 0; i < words; i++) {
    uint32_t expected = (uint32_t)(i * 2654435761u) ^ 0xA5A5A5A5u;
    if (buf[i] != expected) {
      if (errors < 4) {
        Serial.printf("  Mismatch at word %u: wrote 0x%08X read 0x%08X\n",
                      (unsigned)i, expected, buf[i]);
      }
      errors++;
    }
  }

  heap_caps_free(buf);

  if (errors) {
    Serial.printf("  %u mismatched words out of %u\n",
                  (unsigned)errors, (unsigned)words);
    return false;
  }
  return true;
}

static void printChipInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);

  Serial.println("--- Chip ---");
  Serial.printf("Model              : %s\n", ESP.getChipModel());
  Serial.printf("Revision           : v%d.%d\n", chip.revision / 100, chip.revision % 100);
  Serial.printf("CPU Cores          : %d\n", chip.cores);
  Serial.printf("CPU Frequency      : %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());

  Serial.print("Features           : ");
  if (chip.features & CHIP_FEATURE_EMB_FLASH) Serial.print("Embedded Flash | ");
  if (chip.features & CHIP_FEATURE_EMB_PSRAM) Serial.print("Embedded PSRAM | ");
  if (chip.features & CHIP_FEATURE_WIFI_BGN)  Serial.print("WiFi b/g/n | ");
  if (chip.features & CHIP_FEATURE_BLE)       Serial.print("BLE | ");
  if (chip.features & CHIP_FEATURE_BT)        Serial.print("BT Classic | ");
  Serial.println();

  if (chip.cores < 2) fail("Expected 2 CPU cores");

  // esp_read_mac() returns bytes in the correct order. ESP.getEfuseMac()
  // returns them reversed, which is why the naive shift-and-mask print
  // comes out backwards compared to esptool.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("MAC (WiFi STA)     : %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.printf("Reset Reason       : %s\n", resetReasonToString(esp_reset_reason()));
  Serial.println();
}

static void printFlashInfo() {
  Serial.println("--- Flash ---");

  uint32_t sizeBytes = ESP.getFlashChipSize();
  Serial.printf("Total Size         : %lu MB (%lu KB)\n",
                (unsigned long)(sizeBytes / (1024UL * 1024UL)),
                (unsigned long)(sizeBytes / 1024UL));
  Serial.printf("Bus Speed          : %lu MHz\n",
                (unsigned long)(ESP.getFlashChipSpeed() / 1000000UL));
  Serial.printf("Sketch Used        : %lu KB\n", (unsigned long)(ESP.getSketchSize() / 1024UL));
  Serial.printf("Sketch Free        : %lu KB\n", (unsigned long)(ESP.getFreeSketchSpace() / 1024UL));

  // N16R8 must report 16 MB. Anything less means Flash Size is set wrong,
  // which will corrupt data written past the assumed end of flash.
  if (sizeBytes < 16UL * 1024UL * 1024UL) {
    fail("Flash < 16 MB - set Tools > Flash Size to 16MB (128Mb)");
  }
  Serial.println();
}

static void printRamInfo() {
  Serial.println("--- Internal RAM ---");
  Serial.printf("Free Heap          : %lu bytes (%.1f KB)\n",
                (unsigned long)ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
  Serial.printf("Min Free Ever      : %lu bytes\n", (unsigned long)ESP.getMinFreeHeap());
  Serial.printf("Largest Free Block : %lu bytes\n", (unsigned long)ESP.getMaxAllocHeap());
  Serial.println();
}

static void printPsramInfo() {
  Serial.println("--- PSRAM (critical for camera) ---");

  if (!psramFound()) {
    Serial.println("Detected           : NO");
    fail("PSRAM not found - set Tools > PSRAM to 'OPI PSRAM' (N16R8 is octal)");
    Serial.println();
    return;
  }

  size_t total = ESP.getPsramSize();
  size_t freeP = ESP.getFreePsram();
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

  Serial.println("Detected           : YES");
  Serial.printf("Total              : %lu MB (%lu KB)\n",
                (unsigned long)(total / (1024UL * 1024UL)),
                (unsigned long)(total / 1024UL));
  Serial.printf("Free               : %lu KB\n", (unsigned long)(freeP / 1024UL));

  // Camera frame buffers must be CONTIGUOUS. Plenty of total free PSRAM with a
  // small largest block is the fragmentation failure that makes esp_camera_init
  // fail for reasons that look like a wiring fault.
  Serial.printf("Largest Contiguous : %lu KB\n", (unsigned long)(largest / 1024UL));

  if (total < 8UL * 1024UL * 1024UL) {
    fail("PSRAM < 8 MB - check PSRAM mode / board variant");
  }

  Serial.printf("Integrity Test     : writing %u KB pattern...\n",
                (unsigned)(PSRAM_TEST_BYTES / 1024));
  if (testPsramIntegrity(PSRAM_TEST_BYTES)) {
    Serial.println("Integrity Test     : PASS");
  } else {
    fail("PSRAM read-back mismatch - bad timing mode or faulty module");
  }
  Serial.println();
}

static void printSoftwareInfo() {
  Serial.println("--- Software ---");
  Serial.printf("ESP-IDF Version    : %s\n", esp_get_idf_version());
#ifdef ARDUINO_USB_CDC_ON_BOOT
  Serial.printf("USB CDC On Boot    : %d\n", ARDUINO_USB_CDC_ON_BOOT);
#endif
#ifdef BOARD_HAS_PSRAM
  Serial.println("BOARD_HAS_PSRAM    : defined");
#endif
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // On the S3 with USB CDC this genuinely waits for the host to enumerate.
  // Without it the first few printed characters get dropped, which is what
  // produced the run-together lines in the earlier output.
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  delay(300);

  Serial.println();
  Serial.println("=========================================");
  Serial.println("     ESP32-S3 BOARD HEALTH CHECK         ");
  Serial.println("=========================================");
  Serial.println();

  printChipInfo();
  printFlashInfo();
  printRamInfo();
  printPsramInfo();
  printSoftwareInfo();

  Serial.println("=========================================");
  if (g_allChecksPassed) {
    Serial.println("  RESULT: ALL CHECKS PASSED");
    Serial.println("  Board is ready for camera bring-up.");
  } else {
    Serial.println("  RESULT: ONE OR MORE CHECKS FAILED");
    Serial.println("  Fix the FAIL lines above before writing camera code.");
  }
  Serial.println("=========================================");
}

void loop() {
  // Diagnostics run once. Nothing to do here.
  delay(1000);
}
