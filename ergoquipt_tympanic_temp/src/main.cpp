#include <Arduino.h>

#include "ble_manager.h"
#include "config.h"
#include "sensor_manager.h"

BleManager g_bleManager;
SensorManager g_sensorManager;

namespace {

constexpr uint8_t kConnectionLedPin = 7;
constexpr uint32_t kBlinkIntervalMs = 500;

bool g_ledState = false;
uint32_t g_lastBlinkToggleMs = 0;

void updateConnectionLed(uint32_t nowMs) {
  if (g_bleManager.isConnected()) {
    g_ledState = true;
    digitalWrite(kConnectionLedPin, HIGH);
    return;
  }

  if ((nowMs - g_lastBlinkToggleMs) >= kBlinkIntervalMs) {
    g_lastBlinkToggleMs = nowMs;
    g_ledState = !g_ledState;
    digitalWrite(kConnectionLedPin, g_ledState ? HIGH : LOW);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kConnectionLedPin, OUTPUT);
  digitalWrite(kConnectionLedPin, LOW);
  g_sensorManager.begin();
  g_bleManager.begin();

  Serial.print("BLE ready as: ");
  Serial.println(g_bleManager.deviceName());
}

void loop() {
  const uint32_t nowMs = millis();
  updateConnectionLed(nowMs);

  if (!g_sensorManager.isReadyToSample(nowMs)) {
    return;
  }

  TempSample sample{};
  uint8_t payload[cfg::kPayloadSize] = {0};

  g_sensorManager.acquireSample(nowMs, sample);
  g_sensorManager.encodePayload(sample, payload);
  g_bleManager.publish(payload, sizeof(payload));
}
