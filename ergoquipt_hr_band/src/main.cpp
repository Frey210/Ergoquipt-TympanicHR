#include <Arduino.h>

#include "ble_manager.h"
#include "config.h"
#include "sensor_manager.h"

namespace {

BleManager g_bleManager;
SensorManager g_sensorManager;
uint32_t g_lastPublishMs = 0;

}  // namespace

void setup() {
  Serial.begin(115200);

  g_sensorManager.begin();
  g_bleManager.begin();

  Serial.print("BLE ready as: ");
  Serial.println(g_bleManager.deviceName());
}

void loop() {
  const uint32_t nowMs = millis();

  g_sensorManager.update(nowMs);

  if ((nowMs - g_lastPublishMs) >= cfg::kUpdateIntervalMs) {
    g_lastPublishMs = nowMs;
    g_bleManager.publish(g_sensorManager.getVitalData());
  }

  delay(2);
}