#include <Arduino.h>
#include <esp_system.h>

#include "ble_manager.h"
#include "config.h"

BleManager g_bleManager;

namespace {

uint32_t g_lastPublishMs = 0;
uint8_t g_sequenceCounter = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  g_bleManager.begin();

  Serial.print("BLE dummy mode ready as: ");
  Serial.println(g_bleManager.deviceName());
}

void loop() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastPublishMs) < cfg::kUpdateIntervalMs) {
    return;
  }
  g_lastPublishMs = nowMs;

  // Temperature range for dummy payload: 35.50 C to 39.99 C.
  const int16_t tempX100 = static_cast<int16_t>(3550 + (esp_random() % 450));
  uint8_t status = cfg::kStatusSensorValid;

  // Simulate occasional low-battery flag for mobile handling tests.
  if ((esp_random() % 100) < 5) {
    status |= cfg::kStatusLowBattery;
  }

  uint8_t payload[cfg::kPayloadSize] = {0};
  payload[0] = static_cast<uint8_t>(tempX100 & 0xFF);
  payload[1] = static_cast<uint8_t>((tempX100 >> 8) & 0xFF);
  payload[2] = status;
  payload[3] = g_sequenceCounter++;

  g_bleManager.publish(payload, sizeof(payload));

  Serial.print("dummy temp=");
  Serial.print(static_cast<float>(tempX100) / 100.0F, 2);
  Serial.print(" status=0x");
  Serial.print(status, HEX);
  Serial.print(" seq=");
  Serial.println(payload[3]);
}

