#include <Arduino.h>
#include <esp_system.h>
#include <esp_chip_info.h>  // Added for newer ESP32 Core versions
#include <esp_flash.h>      // Standardized flash API
#include <esp_mac.h>        // NEW: esp_read_mac() + esp_mac_type_t

// NEW: prints one interface MAC, asking the SDK for it rather than
// assuming the derivation offsets. Falls back to a readable error string
// if this chip does not have that interface.
void printMac(const char* label, esp_mac_type_t type) {
  uint8_t mac[8] = {0};              // 8 bytes: 802.15.4 addresses are longer
  esp_err_t err = esp_read_mac(mac, type);
  if (err == ESP_OK) {
    Serial.printf("%-19s: %02X:%02X:%02X:%02X:%02X:%02X\n", label,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    Serial.printf("%-19s: unavailable (%s)\n", label, esp_err_to_name(err));
  }
}

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

  // 2. MAC ADDRESSES — all interfaces
  // The chip holds ONE 48-bit base address in eFuse. Every interface address
  // is derived from it, so all of these belong to this single board.
  Serial.println("\n--- MAC Addresses ---");

  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("%-19s: %02X:%02X:%02X:%02X:%02X:%02X\n", "eFuse (raw)",
                (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
                (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
                (uint8_t)(mac >> 8),  (uint8_t)mac);

  printMac("Base MAC",      ESP_MAC_BASE);        // what the others derive from
  printMac("WiFi Station",  ESP_MAC_WIFI_STA);    // == base; use for ESP-NOW peers
  printMac("WiFi SoftAP",   ESP_MAC_WIFI_SOFTAP); // base + 1
  printMac("Bluetooth",     ESP_MAC_BT);          // base + 2; what a BT scanner shows
  printMac("Ethernet",      ESP_MAC_ETH);         // base + 3; no PHY on this board

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
