#pragma once

#include <Arduino.h>

struct HrSample {
  uint16_t heartRate;
  uint16_t spo2X100;
  uint8_t status;
};

class SensorManager {
 public:
  void begin();
  bool isReadyToSample(uint32_t nowMs) const;
  void acquireSample(uint32_t nowMs, HrSample &sample);
  void encodePayload(const HrSample &sample, uint8_t payload[6]);

 private:
  uint32_t lastSampleMs_ = 0;
  uint8_t sequenceCounter_ = 0;
  bool sensorInitialized_ = false;
};

