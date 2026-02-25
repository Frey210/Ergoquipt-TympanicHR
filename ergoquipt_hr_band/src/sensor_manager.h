#pragma once

#include <Arduino.h>

struct VitalData {
  uint16_t hr;
  uint16_t spo2_x100;
  uint16_t rri_ms;
  uint16_t hrv_ms;
  uint8_t status;
};

class SensorManager {
 public:
  void begin();
  void update(uint32_t nowMs);
  VitalData getVitalData() const;

  uint16_t getRRInterval() const;
  uint16_t getHRV() const;

 private:
  bool initMax30102();
  bool readSample(uint32_t &red, uint32_t &ir);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t &value);
  bool readRegisters(uint8_t reg, uint8_t *buffer, size_t len);

  void processSample(uint32_t nowMs, uint32_t red, uint32_t ir);
  void updateStatus(uint32_t nowMs);

  uint32_t irDc_ = 0;
  uint32_t redDc_ = 0;
  uint32_t filteredIrPrev_ = 0;

  uint32_t lastDataMs_ = 0;

  bool wasAboveThreshold_ = false;
  bool sensorInitialized_ = false;
  bool invalidRriDetected_ = false;

  VitalData vital_{0, 0, 0, 0, 0};
};