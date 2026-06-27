#include "sensor_manager.h"

#include <Wire.h>

#include <algorithm>
#include <cmath>

namespace {

uint32_t averageWindow(const uint32_t *buffer, size_t count) {
  if (count == 0) {
    return 0;
  }

  uint64_t sum = 0;
  for (size_t i = 0; i < count; ++i) {
    sum += buffer[i];
  }
  return static_cast<uint32_t>(sum / count);
}

uint16_t clampU16(uint32_t value) {
  return static_cast<uint16_t>(std::min<uint32_t>(value, 0xFFFFU));
}

}  // namespace

void SensorManager::CircularRriBuffer::push(uint16_t rri) {
  values[head] = rri;
  head = (head + 1U) % cfg::kRriBufferSize;
  count = std::min(count + 1U, static_cast<size_t>(cfg::kRriBufferSize));
}

uint16_t SensorManager::CircularRriBuffer::mean() const {
  if (count == 0) {
    return 0;
  }

  uint32_t sum = 0;
  for (size_t i = 0; i < count; ++i) {
    const size_t idx = (head + cfg::kRriBufferSize - count + i) % cfg::kRriBufferSize;
    sum += values[idx];
  }
  return static_cast<uint16_t>(sum / count);
}

uint16_t SensorManager::CircularRriBuffer::rmssd() const {
  if (count < 2) {
    return 0;
  }

  uint64_t sumSquares = 0;
  for (size_t i = 1; i < count; ++i) {
    const size_t idx = (head + cfg::kRriBufferSize - count + i) % cfg::kRriBufferSize;
    const size_t prevIdx =
        (head + cfg::kRriBufferSize - count + i - 1U) % cfg::kRriBufferSize;
    const int32_t diff =
        static_cast<int32_t>(values[idx]) - static_cast<int32_t>(values[prevIdx]);
    sumSquares += static_cast<uint64_t>(diff * diff);
  }

  const float meanSquares = static_cast<float>(sumSquares) /
                            static_cast<float>(count - 1U);
  return static_cast<uint16_t>(sqrtf(meanSquares));
}

void SensorManager::begin() {
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  Wire.begin(cfg::kI2cSdaPin, cfg::kI2cSclPin, cfg::kI2cFrequencyHz);
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  scanI2cBus();
  sensorReady_ = initSensor();
  imuReady_ = initImu();
  latest_.status = sensorReady_ ? 0 : cfg::kStatusSensorError;

  if (sensorReady_) {
    Serial.printf("MAX3010x initialized, part_id=0x%02X\n", partId_);
  } else {
    Serial.println("MAX3010x init failed on I2C address 0x57");
  }
  Serial.printf("QMI8658 IMU %s on I2C address 0x%02X\n",
                imuReady_ ? "initialized" : "not detected", cfg::kQmi8658Address);
}

bool SensorManager::initSensor() {
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  ok = sensor_.begin(Wire, I2C_SPEED_FAST, cfg::kMax3010xAddress);
  if (ok) {
    partId_ = sensor_.readPartID();
    sensor_.setup(60, 4, 2, 100, 411, 4096);
    sensor_.setPulseAmplitudeRed(0x2F);
    sensor_.setPulseAmplitudeIR(0x2F);
    sensor_.setPulseAmplitudeGreen(0);
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  return ok;
}

bool SensorManager::initImu() {
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  ok = imu_.begin(Wire, cfg::kQmi8658Address, cfg::kI2cSdaPin, cfg::kI2cSclPin);
  if (ok) {
    imu_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                             SensorQMI8658::ACC_ODR_125Hz,
                             SensorQMI8658::LPF_MODE_0);
    imu_.configGyroscope(SensorQMI8658::GYR_RANGE_256DPS,
                         SensorQMI8658::GYR_ODR_112_1Hz,
                         SensorQMI8658::LPF_MODE_0);
    imu_.enableAccelerometer();
    imu_.enableGyroscope();
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  return ok;
}

void SensorManager::scanI2cBus() {
  Serial.printf("I2C scan on SDA=%d SCL=%d\n", cfg::kI2cSdaPin, cfg::kI2cSclPin);

  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }

  uint8_t foundCount = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      ++foundCount;
      Serial.printf("  I2C device found at 0x%02X\n", address);
    }
  }

  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  if (foundCount == 0) {
    Serial.println("  No I2C devices detected");
  }
}

bool SensorManager::readSample(uint32_t &ir, uint32_t &red) {
  if (!sensorReady_) {
    return false;
  }

  bool available = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  available = sensor_.available();
  if (!available) {
    sensor_.check();
    available = sensor_.available();
  }

  if (available) {
    red = sensor_.getRed();
    ir = sensor_.getIR();
    sensor_.nextSample();
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  return available;
}

void SensorManager::sample() {
  if (!enabled_) {
    return;
  }

  const uint32_t nowMs = millis();
  uint32_t ir = 0;
  uint32_t red = 0;

  if (!readSample(ir, red)) {
    if ((nowMs - lastDebugLogMs_) >= 1000U) {
      lastDebugLogMs_ = nowMs;
      Serial.println("MAX3010x: no FIFO sample available");
    }
    return;
  }

  lastIrSample_ = ir;
  lastRedSample_ = red;
  sampleMotion(nowMs);
  processSignals(nowMs, ir, red);

  if ((nowMs - lastDebugLogMs_) >= 1000U) {
    lastDebugLogMs_ = nowMs;
    const VitalData snapshot = latest();
    Serial.printf(
        "Vitals hr=%u spo2=%u.%02u rri=%u hrv=%u status=0x%02X ir=%lu red=%lu "
        "finger=%s sensor=%s part=0x%02X mode=%s motion=%.3f imu=%s peak=%u rri_ok=%u\n",
        snapshot.hr, snapshot.spo2_x100 / 100U, snapshot.spo2_x100 % 100U,
        snapshot.rri, snapshot.hrv, snapshot.status,
        static_cast<unsigned long>(lastIrSample_),
        static_cast<unsigned long>(lastRedSample_),
        fingerPresent_ ? "yes" : "no", sensorReady_ ? "ok" : "fail", partId_,
        modeName(), static_cast<double>(motionScore_), imuReady_ ? "ok" : "missing",
        diagnostics_.peakDetected ? 1U : 0U, diagnostics_.rriAccepted ? 1U : 0U);
  }
}

void SensorManager::setEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }

  enabled_ = enabled;
  if (!sensorReady_) {
    return;
  }

  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  if (enabled_) {
    sensor_.wakeUp();
    sensor_.clearFIFO();
  } else {
    sensor_.shutDown();
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
}

void SensorManager::sampleMotion(uint32_t nowMs) {
  if (!imuReady_ || (nowMs - lastImuSampleMs_) < 20U) {
    return;
  }
  lastImuSampleMs_ = nowMs;

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  if (imu_.getDataReady()) {
    ok = imu_.getAccelerometer(ax, ay, az);
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  if (!ok) {
    return;
  }

  const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  accelX_ = ax;
  accelY_ = ay;
  accelZ_ = az;
  accelMagnitudeRawG_ = magnitude;
  const float delta = fabsf(magnitude - accelMagnitudeG_);
  accelMagnitudeG_ = accelMagnitudeG_ * 0.90f + magnitude * 0.10f;
  motionScore_ = motionScore_ * 0.85f + delta * 0.15f;
}

void SensorManager::processSignals(uint32_t nowMs, uint32_t ir, uint32_t red) {
  irWindow_[signalIndex_] = ir;
  redWindow_[signalIndex_] = red;
  signalIndex_ = (signalIndex_ + 1U) % cfg::kSignalWindowSize;

  spo2IrWindow_[spo2Index_] = ir;
  spo2RedWindow_[spo2Index_] = red;
  spo2Index_ = (spo2Index_ + 1U) % cfg::kSpo2WindowSize;
  spo2Count_ = std::min(spo2Count_ + 1U, static_cast<size_t>(cfg::kSpo2WindowSize));

  const uint32_t windowIr = averageWindow(irWindow_, cfg::kSignalWindowSize);
  const uint32_t filteredRed = averageWindow(redWindow_, cfg::kSignalWindowSize);
  uint32_t filteredIr = windowIr;
  if (usesMotionAdaptiveSmoothing() && !motionStable()) {
    filteredIr = static_cast<uint32_t>((lastFilteredIr_ * 3U + windowIr) / 4U);
  } else if (filteringMode_ == FilteringMode::M3AdaptiveNoise && imuReady_) {
    if (lastNlmsIr_ == 0U) {
      lastNlmsIr_ = windowIr;
    }
    const float motionNoise = constrain((motionScore_ - cfg::kStillMotionThreshold) /
                                            cfg::kHighMotionThreshold,
                                        0.0f, 1.0f);
    const float alpha = 0.55f + (0.35f * motionNoise);
    filteredIr = static_cast<uint32_t>((static_cast<float>(lastNlmsIr_) * alpha) +
                                       (static_cast<float>(windowIr) * (1.0f - alpha)));
    lastNlmsIr_ = filteredIr;
  }

  if (baselineIr_ == 0) {
    baselineIr_ = filteredIr;
  } else {
    baselineIr_ = static_cast<uint32_t>((baselineIr_ * 31U + filteredIr) / 32U);
  }

  fingerPresent_ = filteredIr > cfg::kFingerIrThreshold;
  const int32_t derivative =
      static_cast<int32_t>(filteredIr) - static_cast<int32_t>(lastFilteredIr_);
  const bool peakAllowed = !gatesPeaksWithMotion() || !highMotion();
  bool rriAccepted = false;
  const bool peakDetected =
      peakAllowed && detectPeak(nowMs, filteredIr, derivative, rriAccepted);
  lastFilteredIr_ = filteredIr;
  previousDerivative_ = derivative;
  ++sampleCounter_;

  VitalData updated = latest();
  updated.status = 0;

  if (!sensorReady_ || !fingerPresent_) {
    updated.hr = 0;
    updated.rri = 0;
    updated.hrv = 0;
    updated.spo2_x100 = 0;
    if (!sensorReady_) {
      updated.status |= cfg::kStatusSensorError;
    }
    portENTER_CRITICAL(&dataMux_);
    diagnostics_.irRaw = ir;
    diagnostics_.redRaw = red;
    diagnostics_.irFiltered = filteredIr;
    diagnostics_.accelX = accelX_;
    diagnostics_.accelY = accelY_;
    diagnostics_.accelZ = accelZ_;
    diagnostics_.accelMagnitude = accelMagnitudeRawG_;
    diagnostics_.motionScore = motionScore_;
    diagnostics_.imuReady = imuReady_;
    diagnostics_.fingerPresent = fingerPresent_;
    diagnostics_.peakDetected = peakDetected;
    diagnostics_.rriAccepted = rriAccepted;
    diagnostics_.motionState = highMotion() ? 2U : (motionStable() ? 0U : 1U);
    latest_ = updated;
    portEXIT_CRITICAL(&dataMux_);
    return;
  } else {
    if (updatesSpo2DuringMotion() || motionStable()) {
      updateSpo2();
    }
    updated = latest();
    if (updated.hr > 0 && updated.spo2_x100 > 0) {
      updated.status |= cfg::kStatusVitalsValid;
    }
    if (updated.rri > 0) {
      updated.status |= cfg::kStatusRriValid;
    }
    if (updated.hrv > 0) {
      updated.status |= cfg::kStatusHrvValid;
    }
  }

  portENTER_CRITICAL(&dataMux_);
  diagnostics_.irRaw = ir;
  diagnostics_.redRaw = red;
  diagnostics_.irFiltered = filteredIr;
  diagnostics_.accelX = accelX_;
  diagnostics_.accelY = accelY_;
  diagnostics_.accelZ = accelZ_;
  diagnostics_.accelMagnitude = accelMagnitudeRawG_;
  diagnostics_.motionScore = motionScore_;
  diagnostics_.imuReady = imuReady_;
  diagnostics_.fingerPresent = fingerPresent_;
  diagnostics_.peakDetected = peakDetected;
  diagnostics_.rriAccepted = rriAccepted;
  diagnostics_.motionState = highMotion() ? 2U : (motionStable() ? 0U : 1U);
  latest_ = updated;
  portEXIT_CRITICAL(&dataMux_);

  (void)peakDetected;
  (void)filteredRed;
}

bool SensorManager::detectPeak(uint32_t nowMs, uint32_t filteredIr,
                               int32_t derivative, bool &rriAccepted) {
  rriAccepted = false;
  if (!fingerPresent_) {
    lastPeakMs_ = 0;
    return false;
  }

  const uint32_t amplitude = (filteredIr > baselineIr_) ? (filteredIr - baselineIr_) : 0;
  const uint32_t adaptiveThreshold =
      std::max<uint32_t>(baselineIr_ / (motionStable() || filteringMode_ == FilteringMode::M0NoImu
                                            ? 45U
                                            : 32U),
                         120U);

  if (!(previousDerivative_ > 0 && derivative <= 0 &&
        amplitude > adaptiveThreshold)) {
    return false;
  }

  if (lastPeakMs_ == 0U) {
    lastPeakMs_ = nowMs;
    lastPeakAmplitude_ = amplitude;
    return true;
  }

  const uint32_t rri = nowMs - lastPeakMs_;
  if (rri < cfg::kMinRriMs) {
    if (amplitude > lastPeakAmplitude_) {
      lastPeakMs_ = nowMs;
      lastPeakAmplitude_ = amplitude;
    }
    return true;
  }

  if (rri > cfg::kMaxRriMs) {
    lastPeakMs_ = nowMs;
    lastPeakAmplitude_ = amplitude;
    return true;
  }

  rriBuffer_.push(static_cast<uint16_t>(rri));

  const uint16_t meanRri = rriBuffer_.mean();
  const uint16_t bpm = meanRri > 0 ? static_cast<uint16_t>(60000U / meanRri) : 0;
  const uint16_t rmssd = rriBuffer_.rmssd();

  portENTER_CRITICAL(&dataMux_);
  latest_.rri = static_cast<uint16_t>(rri);
  latest_.hr = bpm;
  latest_.hrv = rmssd;
  portEXIT_CRITICAL(&dataMux_);

  lastPeakMs_ = nowMs;
  lastPeakAmplitude_ = amplitude;
  lastAcceptedRriMs_ = nowMs;
  rriAccepted = true;
  return true;
}

void SensorManager::updateSpo2() {
  if (spo2Count_ < 25U) {
    return;
  }

  uint32_t irMin = UINT32_MAX;
  uint32_t irMax = 0;
  uint32_t redMin = UINT32_MAX;
  uint32_t redMax = 0;
  uint64_t irSum = 0;
  uint64_t redSum = 0;

  for (size_t i = 0; i < spo2Count_; ++i) {
    irMin = std::min(irMin, spo2IrWindow_[i]);
    irMax = std::max(irMax, spo2IrWindow_[i]);
    redMin = std::min(redMin, spo2RedWindow_[i]);
    redMax = std::max(redMax, spo2RedWindow_[i]);
    irSum += spo2IrWindow_[i];
    redSum += spo2RedWindow_[i];
  }

  const float irDc = static_cast<float>(irSum) / static_cast<float>(spo2Count_);
  const float redDc = static_cast<float>(redSum) / static_cast<float>(spo2Count_);
  const float irAc = static_cast<float>(irMax - irMin);
  const float redAc = static_cast<float>(redMax - redMin);

  if (irDc < 1.0f || redDc < 1.0f || irAc < 1.0f || redAc < 1.0f) {
    return;
  }

  const float ratio = (redAc / redDc) / (irAc / irDc);
  float spo2 = 110.0f - 25.0f * ratio;
  spo2 = std::min(100.0f, std::max(70.0f, spo2));

  portENTER_CRITICAL(&dataMux_);
  latest_.spo2_x100 = clampU16(static_cast<uint32_t>(spo2 * 100.0f));
  portEXIT_CRITICAL(&dataMux_);
}

bool SensorManager::motionStable() const {
  return !imuReady_ || motionScore_ <= cfg::kStillMotionThreshold;
}

bool SensorManager::highMotion() const {
  return imuReady_ && motionScore_ >= cfg::kHighMotionThreshold;
}

bool SensorManager::usesMotionAdaptiveSmoothing() const {
  return filteringMode_ == FilteringMode::M2MotionAdaptive;
}

bool SensorManager::gatesPeaksWithMotion() const {
  return filteringMode_ == FilteringMode::M1MotionGating ||
         filteringMode_ == FilteringMode::M2MotionAdaptive ||
         filteringMode_ == FilteringMode::M3AdaptiveNoise;
}

bool SensorManager::updatesSpo2DuringMotion() const {
  return filteringMode_ == FilteringMode::M0NoImu;
}

VitalData SensorManager::latest() const {
  VitalData snapshot;
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  snapshot = latest_;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return snapshot;
}

SensorDiagnostics SensorManager::diagnostics() const {
  SensorDiagnostics snapshot;
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  snapshot = diagnostics_;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return snapshot;
}

void SensorManager::setFilteringMode(FilteringMode mode) {
  if (filteringMode() == mode) {
    return;
  }
  resetProcessingState();
  portENTER_CRITICAL(&dataMux_);
  filteringMode_ = mode;
  portEXIT_CRITICAL(&dataMux_);
  Serial.printf("Sensor: filtering mode=%s\n", modeName());
}

FilteringMode SensorManager::filteringMode() const {
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  const FilteringMode mode = filteringMode_;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return mode;
}

const char *SensorManager::modeName() const {
  switch (filteringMode_) {
    case FilteringMode::M0NoImu:
      return "M0";
    case FilteringMode::M1MotionGating:
      return "M1";
    case FilteringMode::M2MotionAdaptive:
      return "M2";
    case FilteringMode::M3AdaptiveNoise:
      return "M3";
    default:
      return "M?";
  }
}

void SensorManager::resetProcessingState() {
  portENTER_CRITICAL(&dataMux_);
  latest_ = VitalData{};
  diagnostics_.peakDetected = false;
  diagnostics_.rriAccepted = false;
  std::fill_n(irWindow_, cfg::kSignalWindowSize, 0U);
  std::fill_n(redWindow_, cfg::kSignalWindowSize, 0U);
  std::fill_n(spo2IrWindow_, cfg::kSpo2WindowSize, 0U);
  std::fill_n(spo2RedWindow_, cfg::kSpo2WindowSize, 0U);
  rriBuffer_ = CircularRriBuffer{};
  signalIndex_ = 0;
  spo2Index_ = 0;
  spo2Count_ = 0;
  baselineIr_ = 0;
  lastFilteredIr_ = 0;
  lastPeakAmplitude_ = 0;
  previousDerivative_ = 0;
  lastPeakMs_ = 0;
  lastAcceptedRriMs_ = 0;
  lastNlmsIr_ = 0;
  sampleCounter_ = 0;
  fingerPresent_ = false;
  portEXIT_CRITICAL(&dataMux_);
}

uint8_t SensorManager::batteryPercent() const { return cfg::kMockBatteryStartPct; }

bool SensorManager::sensorReady() const { return sensorReady_; }

bool SensorManager::fingerPresent() const { return fingerPresent_; }

uint32_t SensorManager::lastIrSample() const { return lastIrSample_; }

uint32_t SensorManager::lastRedSample() const { return lastRedSample_; }

uint8_t SensorManager::partId() const { return partId_; }
