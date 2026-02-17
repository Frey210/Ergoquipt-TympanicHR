#include "sensor_manager.h"

#include "config.h"

namespace {

int16_t placeholderTemperatureX100(uint32_t nowMs) {
  const int16_t base = 3675;
  const int16_t wave = static_cast<int16_t>(((nowMs / 1000U) % 4U) * 5U);
  return static_cast<int16_t>(base + wave);
}

}  // namespace

void SensorManager::begin() {
  // Placeholder for MLX90614 init sequence.
  sensorInitialized_ = true;
  lastSampleMs_ = 0;
  sequenceCounter_ = 0;
}

bool SensorManager::isReadyToSample(uint32_t nowMs) const {
  return (nowMs - lastSampleMs_) >= cfg::kUpdateIntervalMs;
}

void SensorManager::acquireSample(uint32_t nowMs, TempSample &sample) {
  lastSampleMs_ = nowMs;
  sample.temperatureX100 = placeholderTemperatureX100(nowMs);

  uint8_t status = 0;
  if (sensorInitialized_) {
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

