#pragma once

#include <Arduino.h>

class BleManager {
 public:
  void begin();
  void publish(uint8_t *payload, size_t payloadSize);
  bool isConnected() const;
  const char *deviceName() const;

 private:
  bool deviceConnected_ = false;
  char deviceName_[24] = {0};

  class ServerCallbacks;
  ServerCallbacks *callbacks_ = nullptr;
};
