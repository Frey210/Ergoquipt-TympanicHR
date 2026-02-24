#pragma once

#include <Arduino.h>

struct VitalData {
  uint16_t hr;
  uint16_t spo2_x100;
  uint16_t rr_x100;
  uint16_t hrv_ms;
  uint8_t status;
  uint8_t battery;
};

class SensorManager {
 public:
  void begin();
  void update(uint32_t nowMs);
  VitalData getVitalData() const;

 private:
  bool initMax30102();
  bool readSample(uint32_t &red, uint32_t &ir);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t &value);
  bool readRegisters(uint8_t reg, uint8_t *buffer, size_t len);

  void processSample(uint32_t nowMs, uint32_t red, uint32_t ir);
  void updateStatus(uint32_t nowMs);

  static constexpr size_t kIbiBufferSize = 20;

  VitalData vital_{0, 0, 0, 0, 0, 100};

  uint16_t ibiMs_[kIbiBufferSize] = {0};
  size_t ibiHead_ = 0;
  size_t ibiCount_ = 0;

  uint32_t irDc_ = 0;
  uint32_t redDc_ = 0;
  uint32_t filteredIrPrev_ = 0;

  uint32_t lastBeatMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastDataMs_ = 0;

  bool wasAboveThreshold_ = false;
  bool sensorInitialized_ = false;
};