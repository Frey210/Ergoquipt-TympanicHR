#include "sensor_manager.h"

#include <Wire.h>

#include "config.h"

namespace {

constexpr uint8_t kMlx90614Address = 0x5A;
constexpr uint8_t kMlx90614ObjectTempRegister = 0x07;
constexpr int kI2cSdaPin = 8;
constexpr int kI2cSclPin = 9;

bool readMlxRegister16(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(kMlx90614Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytesRequested = 3;
  if (Wire.requestFrom(static_cast<int>(kMlx90614Address), static_cast<int>(bytesRequested)) !=
      bytesRequested) {
    return false;
  }

  const uint8_t lowByte = Wire.read();
  const uint8_t highByte = Wire.read();
  Wire.read();  // PEC byte, not needed for this payload path.

  value = static_cast<uint16_t>(lowByte | (static_cast<uint16_t>(highByte) << 8));
  return value != 0U && value != 0xFFFFU;
}

}  // namespace

void SensorManager::begin() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(100000);
  lastSampleMs_ = 0;
  sequenceCounter_ = 0;
  sensorInitialized_ = readObjectTemperatureX100(lastValidTemperatureX100_);

  if (!sensorInitialized_) {
    Serial.println("MLX90614 init failed");
  }
}

bool SensorManager::isReadyToSample(uint32_t nowMs) const {
  return (nowMs - lastSampleMs_) >= cfg::kUpdateIntervalMs;
}

void SensorManager::acquireSample(uint32_t nowMs, TempSample &sample) {
  lastSampleMs_ = nowMs;
  const bool readOk = readObjectTemperatureX100(sample.temperatureX100);
  sensorInitialized_ = readOk;

  if (readOk) {
    lastValidTemperatureX100_ = sample.temperatureX100;
  } else {
    sample.temperatureX100 = lastValidTemperatureX100_;
  }

  uint8_t status = 0;
  if (readOk) {
    status |= cfg::kStatusSensorValid;
  } else {
    status |= cfg::kStatusSensorError;
  }
  sample.status = status;
}

void SensorManager::encodePayload(const TempSample &sample, uint8_t payload[4]) {
  payload[0] = static_cast<uint8_t>(sample.temperatureX100 & 0xFF);
  payload[1] = static_cast<uint8_t>((sample.temperatureX100 >> 8) & 0xFF);
  payload[2] = sample.status;
  payload[3] = sequenceCounter_++;
}

bool SensorManager::readObjectTemperatureX100(int16_t &temperatureX100) {
  uint16_t rawValue = 0;
  if (!readMlxRegister16(kMlx90614ObjectTempRegister, rawValue)) {
    return false;
  }

  const float temperatureC = (static_cast<float>(rawValue) * 0.02f) - 273.15f;
  if (!isfinite(temperatureC)) {
    return false;
  }

  temperatureX100 = static_cast<int16_t>(lroundf(temperatureC * 100.0f));
  return true;
}
