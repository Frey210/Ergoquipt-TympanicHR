#pragma once

#include <Arduino.h>
#include <MAX30105.h>
#include <SensorQMI8658.hpp>

#include "config.h"

class SensorManager {
 public:
  void begin();
  void sample();
  void setEnabled(bool enabled);
  void setFilteringMode(FilteringMode mode);
  FilteringMode filteringMode() const;
  VitalData latest() const;
  SensorDiagnostics diagnostics() const;
  uint8_t batteryPercent() const;
  bool sensorReady() const;
  bool fingerPresent() const;
  uint32_t lastIrSample() const;
  uint32_t lastRedSample() const;
  uint8_t partId() const;

 private:
  struct CircularRriBuffer {
    uint16_t values[cfg::kRriBufferSize] = {0};
    size_t head = 0;
    size_t count = 0;

    void push(uint16_t rri);
    uint16_t mean() const;
    uint16_t rmssd() const;
  };

  bool initSensor();
  bool initImu();
  void scanI2cBus();
  bool readSample(uint32_t &ir, uint32_t &red);
  void sampleMotion(uint32_t nowMs);
  void processSignals(uint32_t nowMs, uint32_t ir, uint32_t red);
  bool detectPeak(uint32_t nowMs, uint32_t filteredIr, int32_t derivative,
                  bool &rriAccepted);
  void updateSpo2();
  void resetProcessingState();
  bool motionStable() const;
  bool highMotion() const;
  bool usesMotionAdaptiveSmoothing() const;
  bool gatesPeaksWithMotion() const;
  bool updatesSpo2DuringMotion() const;
  const char *modeName() const;

  MAX30105 sensor_;
  SensorQMI8658 imu_;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  VitalData latest_;
  SensorDiagnostics diagnostics_;
  FilteringMode filteringMode_ = FilteringMode::M2MotionAdaptive;

  CircularRriBuffer rriBuffer_;
  uint32_t irWindow_[cfg::kSignalWindowSize] = {0};
  uint32_t redWindow_[cfg::kSignalWindowSize] = {0};
  uint32_t spo2IrWindow_[cfg::kSpo2WindowSize] = {0};
  uint32_t spo2RedWindow_[cfg::kSpo2WindowSize] = {0};
  size_t signalIndex_ = 0;
  size_t spo2Index_ = 0;
  size_t spo2Count_ = 0;

  uint32_t baselineIr_ = 0;
  uint32_t lastFilteredIr_ = 0;
  uint32_t lastPeakAmplitude_ = 0;
  int32_t previousDerivative_ = 0;
  uint32_t lastPeakMs_ = 0;
  uint32_t lastAcceptedRriMs_ = 0;
  uint32_t sampleCounter_ = 0;
  uint32_t lastDebugLogMs_ = 0;
  uint32_t lastImuSampleMs_ = 0;
  uint32_t lastIrSample_ = 0;
  uint32_t lastRedSample_ = 0;
  uint32_t lastNlmsIr_ = 0;
  uint8_t partId_ = 0;
  float accelMagnitudeG_ = 1.0f;
  float motionScore_ = 0.0f;
  float accelX_ = 0.0f;
  float accelY_ = 0.0f;
  float accelZ_ = 0.0f;
  float accelMagnitudeRawG_ = 1.0f;

  bool sensorReady_ = false;
  bool imuReady_ = false;
  bool fingerPresent_ = false;
  bool enabled_ = true;
};
