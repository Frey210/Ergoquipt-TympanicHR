#include "sensor_manager.h"

#include "config.h"

namespace {

uint16_t placeholderHeartRate(uint32_t nowMs) {
  const uint16_t base = 72;
  const uint16_t wave = (nowMs / 1000U) % 6U;
  return static_cast<uint16_t>(base + wave);
}

uint16_t placeholderSpo2X100(uint32_t nowMs) {
  const uint16_t base = 9750;
  const uint16_t wave = ((nowMs / 1000U) % 5U) * 10U;
  return static_cast<uint16_t>(base + wave);
}

}  // namespace

void SensorManager::begin() {
  // Placeholder for MAX30102 init sequence.
  sensorInitialized_ = true;
  lastSampleMs_ = 0;
  sequenceCounter_ = 0;
}

bool SensorManager::isReadyToSample(uint32_t nowMs) const {
  return (nowMs - lastSampleMs_) >= cfg::kUpdateIntervalMs;
}

void SensorManager::acquireSample(uint32_t nowMs, HrSample &sample) {
  lastSampleMs_ = nowMs;

  sample.heartRate = placeholderHeartRate(nowMs);
  sample.spo2X100 = placeholderSpo2X100(nowMs);

  uint8_t status = 0;
  if (sensorInitialized_) {
    status |= cfg::kStatusSensorValid;
  } else {
    status |= cfg::kStatusSensorError;
  }
  sample.status = status;
}

void SensorManager::encodePayload(const HrSample &sample, uint8_t payload[6]) {
  payload[0] = static_cast<uint8_t>(sample.heartRate & 0xFF);
  payload[1] = static_cast<uint8_t>((sample.heartRate >> 8) & 0xFF);
  payload[2] = static_cast<uint8_t>(sample.spo2X100 & 0xFF);
  payload[3] = static_cast<uint8_t>((sample.spo2X100 >> 8) & 0xFF);
  payload[4] = sample.status;
  payload[5] = sequenceCounter_++;
}

