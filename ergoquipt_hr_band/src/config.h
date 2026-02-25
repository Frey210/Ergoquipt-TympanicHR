#pragma once

#include <Arduino.h>

namespace cfg {

constexpr char kDeviceNamePrefix[] = "Ergoquipt-HR";
constexpr char kServiceUuid[] = "e0020001-7cce-4c2a-9f0b-112233445566";
constexpr char kCharacteristicUuid[] = "e0020002-7cce-4c2a-9f0b-112233445566";

constexpr uint32_t kUpdateIntervalMs = 1000;
constexpr size_t kPayloadSize = 12;

constexpr uint8_t kStatusVitalsValid = 1U << 0;
constexpr uint8_t kStatusSensorError = 1U << 1;
constexpr uint8_t kStatusRriValid = 1U << 2;
constexpr uint8_t kStatusHrvValid = 1U << 3;
constexpr uint8_t kStatusLowBattery = 1U << 4;

constexpr uint8_t kBatteryLowThresholdPct = 20;
constexpr uint8_t kBatteryDefaultPct = 100;

constexpr uint8_t kMax30102Address = 0x57;
constexpr uint32_t kSensorStaleTimeoutMs = 3000;

constexpr uint16_t kMinRriMs = 300;
constexpr uint16_t kMaxRriMs = 2000;
constexpr uint16_t kMinHrvMs = 10;
constexpr uint16_t kMaxHrvMs = 300;

}  // namespace cfg