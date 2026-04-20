#pragma once

#include <Arduino.h>

#include "config.h"

class BleManager {
 public:
  void begin();
  void publishLatest(const VitalData &data);
  bool isConnected() const;
  const char *deviceName() const;

 private:
  void packPayload(const VitalData &data, uint8_t payload[cfg::kPayloadSize]);

  bool deviceConnected_ = false;
  char deviceName_[24] = {0};
  uint8_t sequenceCounter_ = 0;

  class ServerCallbacks;
  ServerCallbacks *callbacks_ = nullptr;
};
