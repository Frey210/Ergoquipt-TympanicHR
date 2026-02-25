#include "ble_manager.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_system.h>

#include "config.h"

namespace {

void buildDeviceName(char *outName, size_t outSize) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(outName, outSize, "%s-%01X%02X", cfg::kDeviceNamePrefix, mac[4] & 0x0F, mac[5]);
}

inline void encodeLe16(uint8_t *buffer, size_t offset, uint16_t value) {
  buffer[offset] = static_cast<uint8_t>(value & 0xFFU);
  buffer[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

}  // namespace

class BleManager::ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleManager *owner) : owner_(owner) {}

  void onConnect(BLEServer * /*server*/) override {
    if (owner_ != nullptr) {
      owner_->deviceConnected_ = true;
    }
  }

  void onDisconnect(BLEServer *server) override {
    if (owner_ != nullptr) {
      owner_->deviceConnected_ = false;
    }
    if (server != nullptr) {
      server->getAdvertising()->start();
    }
  }

 private:
  BleManager *owner_;
};

void BleManager::begin() {
  buildDeviceName(deviceName_, sizeof(deviceName_));

  BLEDevice::init(deviceName_);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  static BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  server_ = BLEDevice::createServer();

  static ServerCallbacks callbacks(this);
  callbacksHandle_ = &callbacks;
  server_->setCallbacks(callbacksHandle_);

  BLEService *service = server_->createService(cfg::kServiceUuid);

  characteristic_ = service->createCharacteristic(
      cfg::kCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  static BLE2902 cccd;
  cccd.setNotifications(true);
  characteristic_->addDescriptor(&cccd);

  service->start();
  BLEAdvertising *advertising = server_->getAdvertising();
  advertising->addServiceUUID(cfg::kServiceUuid);
  advertising->start();
}

void BleManager::publish(const VitalData &data) {
  if (!deviceConnected_ || characteristic_ == nullptr) {
    return;
  }

  uint8_t payload[cfg::kPayloadSize] = {0};

  encodeLe16(payload, 0U, data.hr);
  encodeLe16(payload, 2U, data.spo2_x100);
  encodeLe16(payload, 4U, data.rri_ms);
  encodeLe16(payload, 6U, data.hrv_ms);
  payload[8] = data.status;
  payload[9] = sequenceCounter_++;
  payload[10] = 0x00U;
  payload[11] = 0x00U;

  characteristic_->setValue(payload, sizeof(payload));
  characteristic_->notify();
}

bool BleManager::isConnected() const { return deviceConnected_; }

const char *BleManager::deviceName() const { return deviceName_; }