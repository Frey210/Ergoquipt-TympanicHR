#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"

class UiManager {
 public:
  void begin();
  void tick(const VitalData &data, bool bleConnected, uint8_t batteryPercent);

 private:
  void createScreen();
  void updateUi(const VitalData &data, bool bleConnected, uint8_t batteryPercent);
  void updateStatusText(const VitalData &data);
  void setMetricValue(lv_obj_t *label, const char *suffix, uint16_t value,
                      bool valid);

  struct Snapshot {
    VitalData data{};
    bool bleConnected = false;
    uint8_t batteryPercent = 0;
  };

  Snapshot lastSnapshot_{};
  bool initialized_ = false;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastLvTickMs_ = 0;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *titleLabel_ = nullptr;
  lv_obj_t *heartLabel_ = nullptr;
  lv_obj_t *bleLabel_ = nullptr;
  lv_obj_t *batteryLabel_ = nullptr;
  lv_obj_t *statusLabel_ = nullptr;
  lv_obj_t *hrValueLabel_ = nullptr;
  lv_obj_t *spo2ValueLabel_ = nullptr;
  lv_obj_t *rriValueLabel_ = nullptr;
  lv_obj_t *hrvValueLabel_ = nullptr;
};
