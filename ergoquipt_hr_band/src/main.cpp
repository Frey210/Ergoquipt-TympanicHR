#include <Arduino.h>
#include <Wire.h>

#include "ble_manager.h"
#include "config.h"
#include "power_manager.h"
#include "recording_manager.h"
#include "rtc_manager.h"
#include "sensor_manager.h"
#include "ui_manager.h"

SemaphoreHandle_t g_i2cMutex = nullptr;

namespace {

BleManager g_bleManager;
SensorManager g_sensorManager;
UiManager g_uiManager;
PowerManager g_powerManager;
RtcManager g_rtcManager;
RecordingManager g_recordingManager;
bool g_softSleep = false;

void sensorTask(void *parameter) {
  auto *sensorManager = static_cast<SensorManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    sensorManager->sample();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kSensorTaskPeriodMs));
  }
}

VitalData dataWithBatteryStatus(VitalData data) {
  if (g_powerManager.batteryPercent() <= cfg::kLowBatteryThresholdPct) {
    data.status |= cfg::kStatusLowBattery;
  }
  return data;
}

void bleTask(void *parameter) {
  auto *bleManager = static_cast<BleManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (!g_softSleep) {
      bleManager->publishLatest(dataWithBatteryStatus(g_sensorManager.latest()));
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kBlePublishPeriodMs));
  }
}

void recordingTask(void *parameter) {
  auto *recordingManager = static_cast<RecordingManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (!g_softSleep) {
      recordingManager->append(dataWithBatteryStatus(g_sensorManager.latest()),
                               g_powerManager.batteryPercent(),
                               g_bleManager.isConnected(),
                               g_rtcManager.snapshot(),
                               g_sensorManager.filteringMode(),
                               g_sensorManager.diagnostics());
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(cfg::kRecordPeriodMs));
  }
}

void setSoftSleep(bool enabled) {
  if (g_softSleep == enabled) {
    return;
  }

  g_softSleep = enabled;
  if (enabled && g_recordingManager.recording()) {
    g_recordingManager.stop();
  }
  g_sensorManager.setEnabled(!enabled);
  g_bleManager.setEnabled(!enabled);
  g_uiManager.setDisplayOn(!enabled);
  Serial.println(enabled ? "Power: entering soft sleep"
                         : "Power: leaving soft sleep");
}

void uiTask(void *parameter) {
  auto *uiManager = static_cast<UiManager *>(parameter);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    g_powerManager.poll();
    g_rtcManager.poll();
    if (g_powerManager.takeShortPress()) {
      Serial.println("Power: AXP short press");
      setSoftSleep(!g_softSleep);
    }
    if (g_powerManager.takeLongPress()) {
      Serial.println("Power: AXP long press");
      setSoftSleep(!g_softSleep);
    }
    if (g_powerManager.takeBootPress()) {
      Serial.println("BOOT: restarting ESP32");
      Serial.flush();
      delay(100);
      ESP.restart();
    }
    if (!g_softSleep && uiManager->takeRecordingToggleRequest()) {
      if (g_recordingManager.recording()) {
        g_recordingManager.stop();
      } else {
        g_recordingManager.start(g_rtcManager.snapshot());
      }
    }
    FilteringMode requestedMode = g_sensorManager.filteringMode();
    if (!g_softSleep && uiManager->takeFilteringModeRequest(requestedMode)) {
      g_sensorManager.setFilteringMode(requestedMode);
    }
    uiManager->tick(dataWithBatteryStatus(g_sensorManager.latest()), g_bleManager.isConnected(),
                    g_powerManager.batteryPercent(), g_rtcManager.snapshot(),
                    g_bleManager.deviceName(), g_recordingManager.snapshot(),
                    g_sensorManager.filteringMode());
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
  g_powerManager.begin();
  g_recordingManager.begin();
  g_rtcManager.begin();
  g_bleManager.begin();
  g_uiManager.begin();

  Serial.print("BLE device name: ");
  Serial.println(g_bleManager.deviceName());

  xTaskCreatePinnedToCore(sensorTask, "sensor_task", 8192, &g_sensorManager, 3,
                          nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(bleTask, "ble_task", 6144, &g_bleManager, 2, nullptr,
                          APP_CPU_NUM);
  xTaskCreatePinnedToCore(recordingTask, "recording_task", 8192,
                          &g_recordingManager, 1, nullptr, APP_CPU_NUM);
  xTaskCreatePinnedToCore(uiTask, "ui_task", 12288, &g_uiManager, 2, nullptr,
                          PRO_CPU_NUM);
}

void loop() {
  vTaskDelete(nullptr);
}
