#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "recording_manager.h"
#include "rtc_manager.h"

class UiManager {
 public:
  void begin();
  void tick(const VitalData &data, bool bleConnected, uint8_t batteryPercent,
            const RtcSnapshot &rtc, const char *bleDeviceName,
            const RecordingSnapshot &recording, FilteringMode filteringMode);
  bool takeRecordingToggleRequest();
  bool takeFilteringModeRequest(FilteringMode &mode);
  void setDisplayOn(bool on);
  void toggleDisplay();
  bool displayOn() const;

 private:
  void createScreen();
  void createHeader();
  void createNavigation();
  void createDashboard();
  void createTrends();
  void createDevicePage();
  void createRecordPage();
  void updateUi(const VitalData &data, bool bleConnected, uint8_t batteryPercent,
                const RtcSnapshot &rtc, const char *bleDeviceName,
                const RecordingSnapshot &recording, FilteringMode filteringMode);
  void updateRecordingUi(const RecordingSnapshot &recording,
                         FilteringMode filteringMode);
  void updateStatusText(const VitalData &data);
  void updateTrendCharts(const VitalData &data);
  void setPage(uint8_t page);
  void setMetricValue(lv_obj_t *label, const char *suffix, uint16_t value,
                      bool valid);

  struct Snapshot {
    VitalData data{};
    bool bleConnected = false;
    uint8_t batteryPercent = 0;
    bool rtcValid = false;
    bool recording = false;
    bool sdReady = false;
    FilteringMode filteringMode = FilteringMode::M2MotionAdaptive;
    uint32_t rowsWritten = 0;
    char timeText[9] = "";
    char dateText[11] = "";
    char recordFileName[40] = "";
    char recordStatusText[96] = "";
  };

  Snapshot lastSnapshot_{};
  bool initialized_ = false;
  bool displayOn_ = true;
  bool recordingTogglePending_ = false;
  bool filteringModePending_ = false;
  FilteringMode pendingFilteringMode_ = FilteringMode::M2MotionAdaptive;
  uint8_t activePage_ = 0;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastLvTickMs_ = 0;
  uint32_t trendCount_ = 0;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *logoImg_ = nullptr;
  lv_obj_t *timeLabel_ = nullptr;
  lv_obj_t *dateLabel_ = nullptr;
  lv_obj_t *heartLabel_ = nullptr;
  lv_obj_t *bleLabel_ = nullptr;
  lv_obj_t *batteryLabel_ = nullptr;
  lv_obj_t *statusLabel_ = nullptr;
  lv_obj_t *pageDashboard_ = nullptr;
  lv_obj_t *pageTrends_ = nullptr;
  lv_obj_t *pageDevice_ = nullptr;
  lv_obj_t *pageRecord_ = nullptr;
  lv_obj_t *navDashboard_ = nullptr;
  lv_obj_t *navTrends_ = nullptr;
  lv_obj_t *navDevice_ = nullptr;
  lv_obj_t *navRecord_ = nullptr;
  lv_obj_t *hrValueLabel_ = nullptr;
  lv_obj_t *spo2ValueLabel_ = nullptr;
  lv_obj_t *rriValueLabel_ = nullptr;
  lv_obj_t *hrvValueLabel_ = nullptr;
  lv_obj_t *deviceInfoLabel_ = nullptr;
  lv_obj_t *rtcHelpLabel_ = nullptr;
  lv_obj_t *recordStatusLabel_ = nullptr;
  lv_obj_t *recordButton_ = nullptr;
  lv_obj_t *recordButtonLabel_ = nullptr;
  lv_obj_t *modeButtons_[4] = {nullptr};
  lv_chart_series_t *hrSeries_ = nullptr;
  lv_chart_series_t *heroHrSeries_ = nullptr;
  lv_chart_series_t *spo2Series_ = nullptr;
  lv_chart_series_t *rriSeries_ = nullptr;
  lv_chart_series_t *hrvSeries_ = nullptr;
  lv_obj_t *hrTrendChart_ = nullptr;
  lv_obj_t *heroHrTrendChart_ = nullptr;
  lv_obj_t *spo2TrendChart_ = nullptr;
  lv_obj_t *rriTrendChart_ = nullptr;
  lv_obj_t *hrvTrendChart_ = nullptr;
};
