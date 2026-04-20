#include "ble_manager.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_bt.h>
#include <esp_mac.h>

namespace {

BLEServer *g_server = nullptr;
BLECharacteristic *g_characteristic = nullptr;

void buildDeviceName(char *outName, size_t outSize) {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  const uint16_t suffix = static_cast<uint16_t>(((mac[4] & 0x0F) << 8U) | mac[5]);
  snprintf(outName, outSize, "%s-%03X", cfg::kDeviceNamePrefix, suffix);
}

void writeLe16(uint8_t *buffer, size_t offset, uint16_t value) {
  buffer[offset] = static_cast<uint8_t>(value & 0xFF);
  buffer[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFF);
}

}  // namespace

class BleManager::ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleManager *owner) : owner_(owner) {}

  void onConnect(BLEServer * /*server*/) override {
    owner_->deviceConnected_ = true;
    Serial.println("BLE connected");
  }

  void onDisconnect(BLEServer *server) override {
    owner_->deviceConnected_ = false;
    Serial.println("BLE disconnected");
    server->getAdvertising()->start();
  }

 private:
  BleManager *owner_;
};

void BleManager::begin() {
  buildDeviceName(deviceName_, sizeof(deviceName_));

  BLEDevice::init(deviceName_);
  BLEDevice::setMTU(64);

  auto *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security->setCapability(ESP_IO_CAP_NONE);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  g_server = BLEDevice::createServer();
  callbacks_ = new ServerCallbacks(this);
  g_server->setCallbacks(callbacks_);

  BLEService *service = g_server->createService(cfg::kServiceUuid);
  g_characteristic = service->createCharacteristic(
      cfg::kCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  auto *ccc = new BLE2902();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  ccc->setNotifications(true);
  g_characteristic->addDescriptor(ccc);

  uint8_t initialPayload[cfg::kPayloadSize] = {0};
  g_characteristic->setValue(initialPayload, sizeof(initialPayload));

  service->start();

  BLEAdvertising *advertising = g_server->getAdvertising();
  advertising->addServiceUUID(cfg::kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
}

void BleManager::packPayload(const VitalData &data,
                             uint8_t payload[cfg::kPayloadSize]) {
  memset(payload, 0, cfg::kPayloadSize);
  writeLe16(payload, 0, data.hr);
  writeLe16(payload, 2, data.spo2_x100);
  writeLe16(payload, 4, data.rri);
  writeLe16(payload, 6, data.hrv);
  payload[8] = data.status;
  payload[9] = sequenceCounter_++;
  payload[10] = 0x00;
  payload[11] = 0x00;
}

void BleManager::publishLatest(const VitalData &data) {
  uint8_t payload[cfg::kPayloadSize] = {0};
  packPayload(data, payload);

  if (g_characteristic == nullptr) {
    return;
  }

  g_characteristic->setValue(payload, sizeof(payload));
  if (deviceConnected_) {
    g_characteristic->notify();
  }

  Serial.printf("BLE status=%s seq=%u hr=%u rri=%u hrv=%u\n",
                deviceConnected_ ? "connected" : "idle",
                static_cast<unsigned>(payload[9]), data.hr, data.rri, data.hrv);
}

bool BleManager::isConnected() const { return deviceConnected_; }

const char *BleManager::deviceName() const { return deviceName_; }
