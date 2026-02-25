#include "sensor_manager.h"

#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

constexpr uint8_t kRegInterruptEnable1 = 0x02;
constexpr uint8_t kRegInterruptEnable2 = 0x03;
constexpr uint8_t kRegFifoWritePtr = 0x04;
constexpr uint8_t kRegOverflowCounter = 0x05;
constexpr uint8_t kRegFifoReadPtr = 0x06;
constexpr uint8_t kRegFifoData = 0x07;
constexpr uint8_t kRegFifoConfig = 0x08;
constexpr uint8_t kRegModeConfig = 0x09;
constexpr uint8_t kRegSpO2Config = 0x0A;
constexpr uint8_t kRegLed1PulseAmp = 0x0C;
constexpr uint8_t kRegLed2PulseAmp = 0x0D;
constexpr uint8_t kRegPartId = 0xFF;

constexpr uint8_t kExpectedPartId = 0x15;
constexpr uint32_t kBeatRefractoryMs = 250;
constexpr uint8_t kMaxSamplesPerUpdate = 8;

static uint32_t lastBeatTimestamp = 0;
static uint16_t rrIntervalMs = 0;

static uint16_t rriBuffer[20] = {0};
static uint8_t rriIndex = 0;
static uint8_t validSamples = 0;

inline float clampf(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

inline uint16_t clampu16(uint16_t value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

}  // namespace

void SensorManager::begin() {
  Wire.begin();
  Wire.setClock(400000U);

  irDc_ = 0;
  redDc_ = 0;
  filteredIrPrev_ = 0;
  wasAboveThreshold_ = false;
  invalidRriDetected_ = false;
  lastDataMs_ = 0;

  lastBeatTimestamp = 0;
  rrIntervalMs = 0;
  rriIndex = 0;
  validSamples = 0;
  for (uint8_t i = 0; i < 20; ++i) {
    rriBuffer[i] = 0;
  }

  vital_ = {0, 0, 0, 0, 0};

  sensorInitialized_ = initMax30102();
}

void SensorManager::update(uint32_t nowMs) {
  invalidRriDetected_ = false;

  for (uint8_t i = 0; i < kMaxSamplesPerUpdate; ++i) {
    uint32_t red = 0;
    uint32_t ir = 0;
    if (!readSample(red, ir)) {
      break;
    }

    processSample(nowMs, red, ir);
    lastDataMs_ = nowMs;
  }

  updateStatus(nowMs);
}

VitalData SensorManager::getVitalData() const { return vital_; }

uint16_t SensorManager::getRRInterval() const { return vital_.rri_ms; }

uint16_t SensorManager::getHRV() const { return vital_.hrv_ms; }

bool SensorManager::initMax30102() {
  uint8_t partId = 0;
  if (!readRegister(kRegPartId, partId) || partId != kExpectedPartId) {
    return false;
  }

  if (!writeRegister(kRegModeConfig, 0x40U)) {
    return false;
  }
  delay(5);

  bool ok = true;
  ok &= writeRegister(kRegInterruptEnable1, 0x00U);
  ok &= writeRegister(kRegInterruptEnable2, 0x00U);
  ok &= writeRegister(kRegFifoWritePtr, 0x00U);
  ok &= writeRegister(kRegOverflowCounter, 0x00U);
  ok &= writeRegister(kRegFifoReadPtr, 0x00U);
  ok &= writeRegister(kRegFifoConfig, 0x0FU);
  ok &= writeRegister(kRegModeConfig, 0x03U);
  ok &= writeRegister(kRegSpO2Config, 0x27U);
  ok &= writeRegister(kRegLed1PulseAmp, 0x24U);
  ok &= writeRegister(kRegLed2PulseAmp, 0x24U);

  return ok;
}

bool SensorManager::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(cfg::kMax30102Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool SensorManager::readRegister(uint8_t reg, uint8_t &value) {
  return readRegisters(reg, &value, 1U);
}

bool SensorManager::readRegisters(uint8_t reg, uint8_t *buffer, size_t len) {
  Wire.beginTransmission(cfg::kMax30102Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t requested = Wire.requestFrom(static_cast<int>(cfg::kMax30102Address), static_cast<int>(len));
  if (requested != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    if (!Wire.available()) {
      return false;
    }
    buffer[i] = static_cast<uint8_t>(Wire.read());
  }

  return true;
}

bool SensorManager::readSample(uint32_t &red, uint32_t &ir) {
  if (!sensorInitialized_) {
    return false;
  }

  uint8_t writePtr = 0;
  uint8_t readPtr = 0;
  if (!readRegister(kRegFifoWritePtr, writePtr) || !readRegister(kRegFifoReadPtr, readPtr)) {
    sensorInitialized_ = false;
    return false;
  }

  if (writePtr == readPtr) {
    return false;
  }

  uint8_t raw[6] = {0};
  if (!readRegisters(kRegFifoData, raw, sizeof(raw))) {
    sensorInitialized_ = false;
    return false;
  }

  red = (static_cast<uint32_t>(raw[0] & 0x03U) << 16U) |
        (static_cast<uint32_t>(raw[1]) << 8U) |
        static_cast<uint32_t>(raw[2]);

  ir = (static_cast<uint32_t>(raw[3] & 0x03U) << 16U) |
       (static_cast<uint32_t>(raw[4]) << 8U) |
       static_cast<uint32_t>(raw[5]);

  return true;
}

void SensorManager::processSample(uint32_t /*nowMs*/, uint32_t red, uint32_t ir) {
  if (ir == 0U || red == 0U) {
    return;
  }

  if (irDc_ == 0U) {
    irDc_ = ir;
  }
  if (redDc_ == 0U) {
    redDc_ = red;
  }

  irDc_ = ((irDc_ * 31U) + ir) / 32U;
  redDc_ = ((redDc_ * 31U) + red) / 32U;

  const uint32_t irAc = (ir > irDc_) ? (ir - irDc_) : (irDc_ - ir);
  const uint32_t redAc = (red > redDc_) ? (red - redDc_) : (redDc_ - red);

  if (irDc_ > 0U && redDc_ > 0U && irAc > 0U) {
    const float ratio = (static_cast<float>(redAc) / static_cast<float>(redDc_)) /
                        (static_cast<float>(irAc) / static_cast<float>(irDc_));
    const float spo2 = clampf(110.0f - (25.0f * ratio), 70.0f, 100.0f);
    vital_.spo2_x100 = static_cast<uint16_t>(spo2 * 100.0f + 0.5f);
    vital_.status |= cfg::kStatusVitalsValid;
  }

  const uint32_t beatThreshold = (irDc_ / 64U) + 400U;
  const bool aboveThreshold = irAc > beatThreshold;

  if (aboveThreshold && !wasAboveThreshold_ && irAc >= filteredIrPrev_) {
    const uint32_t currentTimestamp = millis();

    if (lastBeatTimestamp != 0U && (currentTimestamp - lastBeatTimestamp) > kBeatRefractoryMs) {
      const uint32_t rawInterval = currentTimestamp - lastBeatTimestamp;

      if (rawInterval < cfg::kMinRriMs || rawInterval > cfg::kMaxRriMs) {
        invalidRriDetected_ = true;
      } else {
        rrIntervalMs = static_cast<uint16_t>(rawInterval);
        rrIntervalMs = clampu16(rrIntervalMs, cfg::kMinRriMs, cfg::kMaxRriMs);

        vital_.rri_ms = rrIntervalMs;
        vital_.hr = static_cast<uint16_t>(60000U / rrIntervalMs);
        vital_.status |= cfg::kStatusRriValid;
        vital_.status |= cfg::kStatusVitalsValid;

        rriBuffer[rriIndex] = rrIntervalMs;
        rriIndex = static_cast<uint8_t>((rriIndex + 1U) % 20U);
        if (validSamples < 20U) {
          ++validSamples;
        }

        if (validSamples >= 2U) {
          const uint8_t oldestIndex = static_cast<uint8_t>((rriIndex + 20U - validSamples) % 20U);

          float squaredDiffSum = 0.0f;
          uint8_t diffCount = 0;

          uint16_t prev = rriBuffer[oldestIndex];
          for (uint8_t i = 1U; i < validSamples; ++i) {
            const uint8_t idx = static_cast<uint8_t>((oldestIndex + i) % 20U);
            const uint16_t curr = rriBuffer[idx];
            const float diff = static_cast<float>(static_cast<int32_t>(curr) - static_cast<int32_t>(prev));
            squaredDiffSum += diff * diff;
            prev = curr;
            ++diffCount;
          }

          if (diffCount > 0U) {
            const float rmssd = sqrtf(squaredDiffSum / static_cast<float>(diffCount));
            uint16_t hrv = static_cast<uint16_t>(rmssd + 0.5f);
            hrv = clampu16(hrv, cfg::kMinHrvMs, cfg::kMaxHrvMs);

            vital_.hrv_ms = hrv;
            vital_.status |= cfg::kStatusHrvValid;
          }
        }
      }
    }

    lastBeatTimestamp = currentTimestamp;
  }

  filteredIrPrev_ = irAc;
  wasAboveThreshold_ = aboveThreshold;
}

void SensorManager::updateStatus(uint32_t nowMs) {
  vital_.status &= static_cast<uint8_t>(~cfg::kStatusSensorError);

  const bool stale = (lastDataMs_ == 0U) || ((nowMs - lastDataMs_) > cfg::kSensorStaleTimeoutMs);
  if (!sensorInitialized_ || stale || invalidRriDetected_) {
    vital_.status |= cfg::kStatusSensorError;
  }
}