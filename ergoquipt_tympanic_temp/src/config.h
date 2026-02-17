#pragma once

#include <Arduino.h>

namespace cfg {

constexpr char kDeviceNamePrefix[] = "Ergoquipt-TEMP";
constexpr char kServiceUuid[] = "e0010001-7cce-4c2a-9f0b-112233445566";
constexpr char kCharacteristicUuid[] = "e0010002-7cce-4c2a-9f0b-112233445566";

constexpr uint32_t kUpdateIntervalMs = 1000;
constexpr size_t kPayloadSize = 4;

constexpr uint8_t kStatusSensorValid = 1U << 0;
constexpr uint8_t kStatusSensorError = 1U << 1;
constexpr uint8_t kStatusLowBattery = 1U << 2;

}  // namespace cfg

