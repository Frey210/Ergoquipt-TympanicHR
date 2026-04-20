#include <Arduino.h>
#include <Wire.h>

#include "ble_manager.h"
#include "config.h"
#include "sensor_manager.h"
#include "ui_manager.h"

SemaphoreHandle_t g_i2cMutex = nullptr;

namespace {

BleManager g_bleManager;
SensorManager g_sensorManager;
UiManager g_uiManager;

void sensorTask(void *parameter) {
  auto *sensorManager = static_cast<SensorManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    sensorManager->sample();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kSensorTaskPeriodMs));
  }
}

void bleTask(void *parameter) {
  auto *bleManager = static_cast<BleManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    bleManager->publishLatest(g_sensorManager.latest());
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kBlePublishPeriodMs));
  }
}

void uiTask(void *parameter) {
  auto *uiManager = static_cast<UiManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    uiManager->tick(g_sensorManager.latest(), g_bleManager.isConnected(),
                    g_sensorManager.batteryPercent());
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kUiTaskPeriodMs));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  while (!Serial && millis() < 4000U) {
  }
  Serial.println();
  Serial.println("Boot: ergoquipt_hr_band");

  g_i2cMutex = xSemaphoreCreateMutex();
  if (g_i2cMutex == nullptr) {
    Serial.println("FATAL: failed to create I2C mutex");
    return;
  }

  if (psramFound()) {
    Serial.printf("PSRAM detected: %u bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM not detected, using internal RAM");
  }

  g_sensorManager.begin();
  g_bleManager.begin();
  g_uiManager.begin();

  Serial.print("BLE device name: ");
  Serial.println(g_bleManager.deviceName());

  xTaskCreatePinnedToCore(sensorTask, "sensor_task", 8192, &g_sensorManager, 3,
                          nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(bleTask, "ble_task", 6144, &g_bleManager, 2, nullptr,
                          APP_CPU_NUM);
  xTaskCreatePinnedToCore(uiTask, "ui_task", 12288, &g_uiManager, 2, nullptr,
                          PRO_CPU_NUM);
}

void loop() {
  vTaskDelete(nullptr);
}
