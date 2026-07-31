#include <BLEDevice.h> 
#include <BLEServer.h> 
#include <BLEUtils.h> 
#include <BLE2902.h> 

// Use distinct global pointers for TX and RX
BLECharacteristic *pTxCharacteristic; 
BLECharacteristic *pRxCharacteristic; 

bool deviceConnected = false; 
long lastMsg = 0; 
String rxload = "Test\n"; 
  
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" 
  
class MyServerCallbacks: public BLEServerCallbacks { 
    void onConnect(BLEServer* pServer) { 
      deviceConnected = true; 
    }; 
    void onDisconnect(BLEServer* pServer) { 
      deviceConnected = false; 
      // Restart advertising so another device can connect after disconnect
      pServer->getAdvertising()->start();
    } 
}; 
  
class MyCallbacks: public BLECharacteristicCallbacks { 
    void onWrite(BLECharacteristic *pCharacteristic) { 
      // Returns an Arduino String directly on ESP32 Core v3+
      String rxValue = pCharacteristic->getValue(); 
      if (rxValue.length() > 0) { 
        rxload = rxValue; 
      } 
    } 
};
 
void setupBLE(String BLEName) { 
  BLEDevice::init(BLEName.c_str()); 
  BLEServer *pServer = BLEDevice::createServer(); 
  pServer->setCallbacks(new MyServerCallbacks()); 
  
  BLEService *pService = pServer->createService(SERVICE_UUID);  
  
  // Create TX Characteristic (Notifications)
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX, 
                        BLECharacteristic::PROPERTY_NOTIFY
                      ); 
  pTxCharacteristic->addDescriptor(new BLE2902()); 

  // Create RX Characteristic (Writes)
  pRxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX, 
                        BLECharacteristic::PROPERTY_WRITE
                      ); 
  pRxCharacteristic->setCallbacks(new MyCallbacks());  
  
  pService->start(); 

  // Configure and start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // Helps with iPhone connection issues
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Waiting for a client connection to notify..."); 
} 
 
void setup() { 
  Serial.begin(115200); 
  setupBLE("ESP32S3_Bluetooth"); 
} 
  
void loop() { 
  long now = millis(); 
  if (now - lastMsg > 1000) { 
    if (deviceConnected && rxload.length() > 0) { 
        Serial.print("Received: ");
        Serial.println(rxload); 
        rxload = ""; 
    } 
    if (Serial.available() > 0) { 
        String str = Serial.readString(); 
        
        // Use the explicit pTxCharacteristic pointer here
        if (deviceConnected) {
          pTxCharacteristic->setValue(str.c_str()); 
          pTxCharacteristic->notify(); 
          Serial.println("Notified BLE client!");
        } else {
          Serial.println("Cannot send: No BLE device connected.");
        }
    } 
    lastMsg = now; 
  } 
}