#pragma once

#include <Arduino.h>

struct TempSample {
  int16_t temperatureX100;
  uint8_t status;
};

class SensorManager {
 public:
  void begin();
  bool isReadyToSample(uint32_t nowMs) const;
  void acquireSample(uint32_t nowMs, TempSample &sample);
  void encodePayload(const TempSample &sample, uint8_t payload[4]);

 private:
  uint32_t lastSampleMs_ = 0;
  uint8_t sequenceCounter_ = 0;
  bool sensorInitialized_ = false;
};

