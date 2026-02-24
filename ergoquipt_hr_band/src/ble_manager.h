#pragma once

#include <Arduino.h>

#include "sensor_manager.h"

class BLEServer;
class BLECharacteristic;

class BleManager {
 public:
  void begin();
  void publish(const VitalData &data);
  bool isConnected() const;
  const char *deviceName() const;

 private:
  class ServerCallbacks;

  bool deviceConnected_ = false;
  uint8_t sequenceCounter_ = 0;
  char deviceName_[24] = {0};

  BLEServer *server_ = nullptr;
  BLECharacteristic *characteristic_ = nullptr;
  ServerCallbacks *callbacksHandle_ = nullptr;
};