#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===============================
// BLE SETTINGS (UUID CUSTOM)
// ===============================
#define DEVICE_NAME "Timpani"

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890AB"
#define CHARACTERISTIC_UUID "12345678-1234-1234-1234-1234567890AC"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;

// Counter paket BLE
unsigned long packetCount = 0;

// ===============================
// MLX90614 SENSOR
// ===============================
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// ===============================
// BLE CALLBACKS
// ===============================
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("====================================");
    Serial.println("📱 BLE Device Connected");
    Serial.println("Mulai mengirim data suhu...");
    Serial.println("====================================");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("====================================");
    Serial.println("❌ BLE Device Disconnected");
    Serial.println("Menunggu koneksi ulang...");
    Serial.println("====================================");

    pServer->startAdvertising();
  }
};

void setup() {
  Serial.begin(115200);
  Wire.begin();  // ESP32-C3 → SDA=8, SCL=9

  // Init MLX90614
  if (!mlx.begin()) {
    Serial.println("❌ ERROR: Sensor MLX90614 tidak terdeteksi! Periksa wiring.");
    while (1)
      ;
  }
  Serial.println("✔ MLX90614 Ready.");

  // Init BLE
  BLEDevice::init(DEVICE_NAME);

  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create Characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  pServer->getAdvertising()->start();

  Serial.println("====================================");
  Serial.println("📡 BLE Advertising Started...");
  Serial.println("Device Name: ESP32C3_TEMP");
  Serial.println("UUID Service: 12345678-1234-1234-1234-1234567890AB");
  Serial.println("UUID Char   : 12345678-1234-1234-1234-1234567890AC");
  Serial.println("====================================");
}

void loop() {
  if (deviceConnected) {
    float suhu = mlx.readObjectTempC();

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.2f", suhu);

    pCharacteristic->setValue((uint8_t *)buffer, strlen(buffer));
    pCharacteristic->notify();

    packetCount++;

    // =======================
    // SERIAL MONITOR OUTPUT
    // =======================
    Serial.print("Packet #");
    Serial.print(packetCount);
    Serial.print(" | Suhu terkirim: ");
    Serial.print(buffer);
    Serial.print(" °C | Timestamp: ");
    Serial.println(millis());

    delay(50);
  }
}