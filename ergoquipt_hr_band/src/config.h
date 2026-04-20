#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_i2cMutex;

struct VitalData {
  uint16_t hr = 0;
  uint16_t spo2_x100 = 0;
  uint16_t rri = 0;
  uint16_t hrv = 0;
  uint8_t status = 0;
};

namespace cfg {

constexpr char kDeviceNamePrefix[] = "Ergoquipt-HR";
constexpr char kServiceUuid[] = "e0020001-7cce-4c2a-9f0b-112233445566";
constexpr char kCharacteristicUuid[] = "e0020002-7cce-4c2a-9f0b-112233445566";

constexpr uint16_t kDisplayWidth = 368;
constexpr uint16_t kDisplayHeight = 448;

constexpr uint8_t kMax3010xAddress = 0x57;
constexpr uint8_t kFt3168Address = 0x38;

constexpr int kI2cSdaPin = 15;
constexpr int kI2cSclPin = 14;
constexpr uint32_t kI2cFrequencyHz = 400000;

// QSPI pins are based on publicly shared board bring-up notes for the
// Waveshare ESP32-S3-Touch-AMOLED-1.8 hardware variant.
constexpr int kDisplayCsPin = 12;
constexpr int kDisplaySckPin = 11;
constexpr int kDisplayD0Pin = 4;
constexpr int kDisplayD1Pin = 5;
constexpr int kDisplayD2Pin = 6;
constexpr int kDisplayD3Pin = 7;
constexpr int kTouchIntPin = 21;

constexpr uint8_t kPayloadSize = 12;

constexpr uint8_t kStatusVitalsValid = 1U << 0;
constexpr uint8_t kStatusSensorError = 1U << 1;
constexpr uint8_t kStatusRriValid = 1U << 2;
constexpr uint8_t kStatusHrvValid = 1U << 3;
constexpr uint8_t kStatusLowBattery = 1U << 4;

constexpr uint32_t kSensorTaskPeriodMs = 10;
constexpr uint32_t kBlePublishPeriodMs = 1000;
constexpr uint32_t kUiTaskPeriodMs = 33;
constexpr uint32_t kUiRefreshPeriodMs = 1000;

constexpr size_t kRriBufferSize = 20;
constexpr size_t kSignalWindowSize = 8;
constexpr size_t kSpo2WindowSize = 100;

constexpr uint32_t kFingerIrThreshold = 18000;
constexpr uint16_t kMinRriMs = 300;
constexpr uint16_t kMaxRriMs = 2000;
constexpr uint16_t kLowBatteryThresholdPct = 20;
constexpr uint8_t kMockBatteryStartPct = 92;

}  // namespace cfg
