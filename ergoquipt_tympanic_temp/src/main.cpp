#include <Arduino.h>

#include "ble_manager.h"
#include "config.h"
#include "sensor_manager.h"

BleManager g_bleManager;
SensorManager g_sensorManager;

void setup() {
  Serial.begin(115200);
  g_sensorManager.begin();
  g_bleManager.begin();

  Serial.print("BLE ready as: ");
  Serial.println(g_bleManager.deviceName());
}

void loop() {
  const uint32_t nowMs = millis();
  if (!g_sensorManager.isReadyToSample(nowMs)) {
    return;
  }

  TempSample sample{};
  uint8_t payload[cfg::kPayloadSize] = {0};

  g_sensorManager.acquireSample(nowMs, sample);
  g_sensorManager.encodePayload(sample, payload);
  g_bleManager.publish(payload, sizeof(payload));
}

