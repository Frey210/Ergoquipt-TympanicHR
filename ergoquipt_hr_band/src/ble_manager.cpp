#include "ble_manager.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_bt.h>
#include <esp_system.h>

#include "config.h"

namespace {

BLEServer *g_server = nullptr;
BLECharacteristic *g_characteristic = nullptr;

void buildDeviceName(char *outName, size_t outSize) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(outName, outSize, "%s-%01X%02X", cfg::kDeviceNamePrefix, mac[4] & 0x0F, mac[5]);
}

}  // namespace

class BleManager::ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleManager *owner) : owner_(owner) {}

  void onConnect(BLEServer * /*server*/) override { owner_->deviceConnected_ = true; }

  void onDisconnect(BLEServer *server) override {
    owner_->deviceConnected_ = false;
    server->getAdvertising()->start();
  }

 private:
  BleManager *owner_;
};

void BleManager::begin() {
  buildDeviceName(deviceName_, sizeof(deviceName_));

  BLEDevice::init(deviceName_);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  auto *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security->setCapability(ESP_IO_CAP_NONE);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  g_server = BLEDevice::createServer();
  callbacks_ = new ServerCallbacks(this);
  g_server->setCallbacks(callbacks_);

  BLEService *service = g_server->createService(cfg::kServiceUuid);

  g_characteristic = service->createCharacteristic(
      cfg::kCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  g_characteristic->addDescriptor(new BLE2902());

  service->start();
  BLEAdvertising *advertising = g_server->getAdvertising();
  advertising->addServiceUUID(cfg::kServiceUuid);
  advertising->start();
}

void BleManager::publish(uint8_t *payload, size_t payloadSize) {
  if (!deviceConnected_ || g_characteristic == nullptr) {
    return;
  }

  g_characteristic->setValue(payload, payloadSize);
  g_characteristic->notify();
}

bool BleManager::isConnected() const { return deviceConnected_; }

const char *BleManager::deviceName() const { return deviceName_; }
