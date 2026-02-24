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
constexpr uint32_t kBeatMinIbiMs = 300;
constexpr uint32_t kBeatMaxIbiMs = 2000;
constexpr uint32_t kBeatRefractoryMs = 250;
constexpr uint8_t kMaxSamplesPerUpdate = 8;

inline float clampf(float value, float minValue, float maxValue) {
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

  vital_ = {0, 0, 0, 0, 0, cfg::kBatteryDefaultPct};
  ibiHead_ = 0;
  ibiCount_ = 0;
  irDc_ = 0;
  redDc_ = 0;
  filteredIrPrev_ = 0;
  lastBeatMs_ = 0;
  lastSampleMs_ = 0;
  lastDataMs_ = 0;
  wasAboveThreshold_ = false;

  sensorInitialized_ = initMax30102();
}

void SensorManager::update(uint32_t nowMs) {
  bool consumedSample = false;

  for (uint8_t i = 0; i < kMaxSamplesPerUpdate; ++i) {
    uint32_t red = 0;
    uint32_t ir = 0;
    if (!readSample(red, ir)) {
      break;
    }
    processSample(nowMs, red, ir);
    consumedSample = true;
  }

  if (consumedSample) {
    lastDataMs_ = nowMs;
  }

  updateStatus(nowMs);
}

VitalData SensorManager::getVitalData() const { return vital_; }

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
  if (!readRegisters(reg, &value, 1U)) {
    return false;
  }
  return true;
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

  lastSampleMs_ = millis();
  return true;
}

void SensorManager::processSample(uint32_t nowMs, uint32_t red, uint32_t ir) {
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
    vital_.status |= cfg::kStatusSpo2Valid;
  }

  const uint32_t beatThreshold = (irDc_ / 64U) + 400U;
  const bool aboveThreshold = irAc > beatThreshold;

  if (aboveThreshold && !wasAboveThreshold_ && irAc >= filteredIrPrev_) {
    const uint32_t elapsedSinceBeat = nowMs - lastBeatMs_;
    if (lastBeatMs_ != 0U && elapsedSinceBeat >= kBeatMinIbiMs && elapsedSinceBeat <= kBeatMaxIbiMs &&
        elapsedSinceBeat > kBeatRefractoryMs) {
      const uint16_t ibiMs = static_cast<uint16_t>(elapsedSinceBeat);
      ibiMs_[ibiHead_] = ibiMs;
      ibiHead_ = (ibiHead_ + 1U) % kIbiBufferSize;
      if (ibiCount_ < kIbiBufferSize) {
        ++ibiCount_;
      }

      if (ibiCount_ >= 1U) {
        uint32_t sum = 0;
        const size_t start = (ibiHead_ + kIbiBufferSize - ibiCount_) % kIbiBufferSize;
        for (size_t i = 0; i < ibiCount_; ++i) {
          sum += ibiMs_[(start + i) % kIbiBufferSize];
        }
        const uint32_t avgIbi = sum / static_cast<uint32_t>(ibiCount_);
        if (avgIbi > 0U) {
          vital_.hr = static_cast<uint16_t>(60000U / avgIbi);
          vital_.status |= cfg::kStatusHrValid;
        }
      }

      if (ibiCount_ >= 3U) {
        const size_t start = (ibiHead_ + kIbiBufferSize - ibiCount_) % kIbiBufferSize;
        float sqDiffSum = 0.0f;
        size_t diffCount = 0;

        uint16_t prev = ibiMs_[start];
        for (size_t i = 1; i < ibiCount_; ++i) {
          const uint16_t current = ibiMs_[(start + i) % kIbiBufferSize];
          const float diff = static_cast<float>(static_cast<int32_t>(current) - static_cast<int32_t>(prev));
          sqDiffSum += diff * diff;
          prev = current;
          ++diffCount;
        }

        if (diffCount > 0U) {
          const float rmssd = sqrtf(sqDiffSum / static_cast<float>(diffCount));
          vital_.hrv_ms = static_cast<uint16_t>(rmssd + 0.5f);
          vital_.status |= cfg::kStatusHrvValid;

          float rrBpm = 12.0f + (static_cast<float>(vital_.hrv_ms) * 0.05f);
          if (vital_.hr > 90U) {
            rrBpm += 1.0f;
          }
          rrBpm = clampf(rrBpm, 8.0f, 30.0f);
          vital_.rr_x100 = static_cast<uint16_t>(rrBpm * 100.0f + 0.5f);
          vital_.status |= cfg::kStatusRrValid;
        }
      }
    }

    lastBeatMs_ = nowMs;
  }

  filteredIrPrev_ = irAc;
  wasAboveThreshold_ = aboveThreshold;
}

void SensorManager::updateStatus(uint32_t nowMs) {
  vital_.battery = cfg::kBatteryDefaultPct;

  if (vital_.battery <= cfg::kBatteryLowThresholdPct) {
    vital_.status |= cfg::kStatusLowBattery;
  } else {
    vital_.status &= static_cast<uint8_t>(~cfg::kStatusLowBattery);
  }

  const bool stale = (lastDataMs_ == 0U) || ((nowMs - lastDataMs_) > cfg::kSensorStaleTimeoutMs);
  if (!sensorInitialized_ || stale) {
    vital_.status |= cfg::kStatusSensorError;
  } else {
    vital_.status &= static_cast<uint8_t>(~cfg::kStatusSensorError);
  }
}