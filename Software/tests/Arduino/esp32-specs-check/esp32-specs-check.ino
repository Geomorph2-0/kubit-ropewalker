#include <Arduino.h>
#include <esp_system.h>
#include <esp_chip_info.h>  // Added for newer ESP32 Core versions
#include <esp_flash.h>      // Standardized flash API

void setup() {
  Serial.begin(115200);
  delay(1000); // Give Serial Monitor time to connect

  Serial.println("\n=========================================");
  Serial.println("         ESP32 HARDWARE SPECS            ");
  Serial.println("=========================================");

  // 1. CHIP REVISION & CORE INFO
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.printf("Chip Model         : %s\n", ESP.getChipModel());
  Serial.printf("Chip Revision      : v%d.%d\n", chip_info.revision / 100, chip_info.revision % 100);
  Serial.printf("CPU Cores          : %d Core(s)\n", chip_info.cores);
  Serial.printf("CPU Frequency      : %d MHz\n", ESP.getCpuFreqMHz());
  
  // Features flags
  Serial.print("Features           : ");
  if (chip_info.features & CHIP_FEATURE_EMB_FLASH) Serial.print("Embedded Flash | ");
  if (chip_info.features & CHIP_FEATURE_WIFI_BGN) Serial.print("Wi-Fi 802.11b/g/n | ");
  if (chip_info.features & CHIP_FEATURE_BLE)      Serial.print("Bluetooth LE | ");
  if (chip_info.features & CHIP_FEATURE_BT)       Serial.print("Bluetooth Classic | ");
  Serial.println();

  // 2. UNIQUE MAC ADDRESS
  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("MAC Address        : %02X:%02X:%02X:%02X:%02X:%02X\n",
                (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
                (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
                (uint8_t)(mac >> 8),  (uint8_t)mac);

  // 3. FLASH MEMORY DETAILS
  Serial.println("\n--- Flash Memory ---");
  Serial.printf("Total Size         : %u MB (%u KB)\n", ESP.getFlashChipSize() / (1024 * 1024), ESP.getFlashChipSize() / 1024);
  Serial.printf("Flash Bus Speed    : %u MHz\n", ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("Used Sketch Space  : %u KB\n", ESP.getSketchSize() / 1024);
  Serial.printf("Free Sketch Space  : %u KB\n", ESP.getFreeSketchSpace() / 1024);

  // 4. RAM / HEAP & PSRAM DETAILS
  Serial.println("\n--- RAM & System Memory ---");
  Serial.printf("Total Free Heap    : %u bytes (%.2f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
  Serial.printf("Max Alloc Block    : %u bytes\n", ESP.getMaxAllocHeap());

  if (psramFound()) {
    Serial.printf("PSRAM Detected     : Yes\n");
    Serial.printf("Total PSRAM        : %u MB (%u KB)\n", ESP.getPsramSize() / (1024 * 1024), ESP.getPsramSize() / 1024);
    Serial.printf("Free PSRAM         : %u KB\n", ESP.getFreePsram() / 1024);
  } else {
    Serial.println("PSRAM Detected     : No (or disabled in board settings)");
  }

  // 5. SDK & CORE VERSION
  Serial.println("\n--- Software Environment ---");
  Serial.printf("ESP-IDF SDK Version: %s\n", esp_get_idf_version());
  Serial.println("=========================================\n");
}

void loop() {
  // Runs once in setup
}