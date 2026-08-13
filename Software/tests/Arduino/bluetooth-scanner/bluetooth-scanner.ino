/*
 * ble_scanner.ino — BLE advertisement scanner for ESP32-S3
 *
 * NOTE: The ESP32-S3 supports Bluetooth LE 5 only. It has NO Bluetooth Classic
 * radio, so classic device inquiry (HC-05, phone audio pairing, SPP) is not
 * possible on this chip. This scans BLE advertisements.
 *
 * Verified target config (Freenove ESP32-S3-WROOM CAM, N16R8):
 *   Board            : ESP32S3 Dev Module
 *   Flash Size       : 16MB (128Mb)
 *   PSRAM            : OPI PSRAM
 *   Partition Scheme : 16M Flash (3MB APP / 9.9MB FATFS)
 *   USB CDC On Boot  : Disabled (flashing via UART bridge port)
 *   Core             : Arduino-ESP32 3.3.10 / ESP-IDF 5.5.4
 *
 * If USB CDC On Boot is Disabled, open the serial monitor on the UART bridge
 * port, not the native USB port, or you will see nothing.
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ---------------------------------------------------------------- tunables --

static const uint32_t SCAN_DURATION_S = 5;      // seconds per scan pass
static const uint32_t PAUSE_BETWEEN_MS = 2000;  // idle gap between passes

// Scan interval/window are in 0.625 ms units.
// Window/Interval ratio == duty cycle. 100% duty is fine when the radio is
// otherwise idle; drop the window if you later add WiFi and see contention.
static const uint16_t SCAN_INTERVAL = 160;  // 100 ms
static const uint16_t SCAN_WINDOW   = 160;  // 100 ms

// Active scan sends SCAN_REQ, which pulls the scan response packet. That is
// where most device names live. Costs a little power and airtime.
static const bool ACTIVE_SCAN = true;

// Filters. Set NAME_FILTER to "" to disable.
static const int  RSSI_FLOOR  = -100;  // ignore anything weaker than this
static const char *NAME_FILTER = "";   // case-sensitive substring match

// ------------------------------------------------------------------ state ---

static BLEScan *pScan = nullptr;
static uint32_t scanPass = 0;
static uint32_t seenThisPass = 0;

// ---------------------------------------------------------------- helpers ---

// Rough distance estimate from RSSI. This is genuinely approximate — multipath,
// body absorption and unknown TX power make it unreliable indoors. Treat it as
// "near / mid / far", not a measurement.
static float estimateMetres(int rssi, int txPowerAt1m = -59) {
  if (rssi == 0) return -1.0f;
  return powf(10.0f, (txPowerAt1m - rssi) / 20.0f);
}

static const char *companyName(uint16_t id) {
  switch (id) {
    case 0x004C: return "Apple";
    case 0x0006: return "Microsoft";
    case 0x00E0: return "Google";
    case 0x0075: return "Samsung";
    case 0x0157: return "Xiaomi";
    case 0x02E5: return "Espressif";
    case 0x0059: return "Nordic Semiconductor";
    default:     return nullptr;
  }
}

static void printHex(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i + 1 < len) Serial.print(' ');
  }
}

// -------------------------------------------------------------- callbacks ---
//
// API NOTE: this uses the legacy BLEAdvertisedDeviceCallbacks interface, which
// still compiles on core 3.x (you may see a deprecation warning). If a future
// core removes it, swap this class for:
//
//   class ScanCallbacks : public BLEScanCallbacks {
//     void onResult(const BLEAdvertisedDevice *dev) override { ... dev-> ... }
//   };
//   pScan->setScanCallbacks(new ScanCallbacks());

class AdvertCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    const int rssi = dev.getRSSI();
    if (rssi < RSSI_FLOOR) return;

    String name = dev.haveName() ? dev.getName() : String();
    if (strlen(NAME_FILTER) > 0) {
      if (name.length() == 0 || name.indexOf(NAME_FILTER) < 0) return;
    }

    seenThisPass++;

    Serial.printf("[%2lu] %s  RSSI %4d dBm  (~%.1f m)\n",
                  (unsigned long)seenThisPass,
                  dev.getAddress().toString().c_str(),
                  rssi,
                  estimateMetres(rssi));

    if (name.length() > 0) {
      Serial.printf("     name      : %s\n", name.c_str());
    }

    if (dev.haveTXPower()) {
      Serial.printf("     tx power  : %d dBm\n", (int)dev.getTXPower());
    }

    if (dev.haveAppearance()) {
      Serial.printf("     appearance: 0x%04X\n", dev.getAppearance());
    }

    if (dev.haveServiceUUID()) {
      Serial.printf("     service   : %s\n",
                    dev.getServiceUUID().toString().c_str());
    }

    if (dev.haveServiceData()) {
      String sd = dev.getServiceData();
      Serial.print("     svc data  : ");
      printHex((const uint8_t *)sd.c_str(), sd.length());
      Serial.println();
    }

    if (dev.haveManufacturerData()) {
      String md = dev.getManufacturerData();
      const uint8_t *raw = (const uint8_t *)md.c_str();
      size_t len = md.length();

      if (len >= 2) {
        // Company ID is little-endian in the first two bytes.
        uint16_t cid = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        const char *cname = companyName(cid);
        Serial.printf("     vendor    : 0x%04X%s%s\n",
                      cid,
                      cname ? " — " : "",
                      cname ? cname : "");
        Serial.print("     mfr data  : ");
        printHex(raw + 2, len - 2);
        Serial.println();
      } else {
        Serial.print("     mfr data  : ");
        printHex(raw, len);
        Serial.println();
      }
    }

    Serial.println();
  }
};

// ------------------------------------------------------------------ setup ---

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  Serial.println();
  Serial.println("=== ESP32-S3 BLE Scanner ===");
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("Bluetooth Classic is not available on the S3 — BLE only.");
  Serial.println();

  // Empty name: we are a scanner, not an advertiser, so do not announce.
  BLEDevice::init("");

  pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertCallbacks(), /*wantDuplicates=*/false);
  pScan->setActiveScan(ACTIVE_SCAN);
  pScan->setInterval(SCAN_INTERVAL);
  pScan->setWindow(SCAN_WINDOW);
}

// ------------------------------------------------------------------- loop ---

void loop() {
  scanPass++;
  seenThisPass = 0;

  Serial.printf("--- pass %lu: scanning for %lu s ---\n",
                (unsigned long)scanPass,
                (unsigned long)SCAN_DURATION_S);

  pScan->start(SCAN_DURATION_S, false);  // blocking

  Serial.printf("--- pass %lu complete: %lu device(s), heap %u ---\n\n",
                (unsigned long)scanPass,
                (unsigned long)seenThisPass,
                (unsigned)ESP.getFreeHeap());

  // Essential: the results vector grows every pass and will exhaust the heap
  // over a long run if you never clear it.
  pScan->clearResults();

  delay(PAUSE_BETWEEN_MS);
}
