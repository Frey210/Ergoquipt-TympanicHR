#include "ui_manager.h"

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "logo_asset.h"

namespace {

Arduino_DataBus *g_bus = nullptr;
Arduino_GFX *g_gfx = nullptr;
lv_disp_draw_buf_t g_drawBuf;
lv_color_t *g_buf1 = nullptr;
lv_color_t *g_buf2 = nullptr;

constexpr size_t kDrawBufferLines = 40;
constexpr uint8_t kFt3168RegNumTouches = 0x02;
constexpr uint8_t kFt3168RegXHigh = 0x03;

bool readTouchPoint(uint16_t &x, uint16_t &y) {
  uint8_t pointCount = 0;

  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }

  Wire.beginTransmission(cfg::kFt3168Address);
  Wire.write(kFt3168RegNumTouches);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(cfg::kFt3168Address, static_cast<uint8_t>(1)) != 1) {
    if (g_i2cMutex != nullptr) {
      xSemaphoreGive(g_i2cMutex);
    }
    return false;
  }

  pointCount = Wire.read() & 0x0F;
  if (pointCount == 0) {
    if (g_i2cMutex != nullptr) {
      xSemaphoreGive(g_i2cMutex);
    }
    return false;
  }

  Wire.beginTransmission(cfg::kFt3168Address);
  Wire.write(kFt3168RegXHigh);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(cfg::kFt3168Address, static_cast<uint8_t>(4)) != 4) {
    if (g_i2cMutex != nullptr) {
      xSemaphoreGive(g_i2cMutex);
    }
    return false;
  }

  const uint8_t xh = Wire.read();
  const uint8_t xl = Wire.read();
  const uint8_t yh = Wire.read();
  const uint8_t yl = Wire.read();

  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  x = static_cast<uint16_t>(((xh & 0x0F) << 8U) | xl);
  y = static_cast<uint16_t>(((yh & 0x0F) << 8U) | yl);
  x = std::min<uint16_t>(x, cfg::kDisplayWidth - 1U);
  y = std::min<uint16_t>(y, cfg::kDisplayHeight - 1U);
  return true;
}

void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint16_t width = static_cast<uint16_t>(area->x2 - area->x1 + 1);
  const uint16_t height = static_cast<uint16_t>(area->y2 - area->y1 + 1);

  g_gfx->draw16bitRGBBitmap(area->x1, area->y1,
                            reinterpret_cast<uint16_t *>(colorP), width, height);
  lv_disp_flush_ready(disp);
}

void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t x = 0;
  uint16_t y = 0;
  if (readTouchPoint(x, y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  (void)drv;
}

lv_obj_t *createCard(lv_obj_t *parent, lv_coord_t width, lv_coord_t height) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_radius(card, 24, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x1D2733), 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0E121A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_100, 0);
  lv_obj_set_style_shadow_width(card, 12, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x020409), 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

lv_obj_t *createCardTitle(lv_obj_t *card, const char *title, const char *icon,
                          lv_color_t color) {
  lv_obj_t *label = lv_label_create(card);
  char text[48];
  snprintf(text, sizeof(text), "%s %s", icon, title);
  lv_label_set_text(label, text);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  return label;
}

lv_obj_t *createNavButton(lv_obj_t *parent, const char *text) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, 104, 34);
  lv_obj_set_style_radius(button, 17, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x111722), 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x243244), 0);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xAAB4C3), 0);
  return button;
}

lv_obj_t *createTrendChart(lv_obj_t *card, lv_color_t color,
                           lv_chart_series_t **series, int16_t minValue,
                           int16_t maxValue) {
  lv_obj_t *chart = lv_chart_create(card);
  lv_obj_set_size(chart, 132, 48);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, cfg::kTrendBufferSize);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, minValue, maxValue);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
  *series = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
  return chart;
}

}  // namespace

void UiManager::begin() {
  pinMode(cfg::kTouchIntPin, INPUT_PULLUP);

  g_bus = new Arduino_ESP32QSPI(cfg::kDisplayCsPin, cfg::kDisplaySckPin,
                                cfg::kDisplayD0Pin, cfg::kDisplayD1Pin,
                                cfg::kDisplayD2Pin, cfg::kDisplayD3Pin);
  g_gfx = new Arduino_SH8601(g_bus, -1, 0, cfg::kDisplayWidth,
                             cfg::kDisplayHeight);

  g_gfx->begin();
  g_gfx->fillScreen(0x0000);
  static_cast<Arduino_SH8601 *>(g_gfx)->setBrightness(220);

  lv_init();

  const size_t pixels = cfg::kDisplayWidth * kDrawBufferLines;
  const size_t bytes = pixels * sizeof(lv_color_t);
  g_buf1 = static_cast<lv_color_t *>(ps_malloc(bytes));
  g_buf2 = static_cast<lv_color_t *>(ps_malloc(bytes));
  if (g_buf1 == nullptr || g_buf2 == nullptr) {
    g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA));
    g_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA));
  }

  lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, pixels);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = cfg::kDisplayWidth;
  dispDrv.ver_res = cfg::kDisplayHeight;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &g_drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);

  createScreen();
  lastLvTickMs_ = millis();
  initialized_ = true;
}

void UiManager::createScreen() {
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_color(screen_, lv_color_hex(0x07101B), 0);
  lv_obj_set_style_bg_grad_dir(screen_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(screen_, 0, 0);
  lv_obj_set_style_pad_all(screen_, 16, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  createHeader();
  createNavigation();
  createDashboard();
  createTrends();
  createDevicePage();
  setPage(0);
  lv_scr_load(screen_);
}

void UiManager::createHeader() {
  logoImg_ = lv_img_create(screen_);
  lv_img_set_src(logoImg_, &ergo_logo_img);
  lv_obj_align(logoImg_, LV_ALIGN_TOP_LEFT, 0, 0);

  timeLabel_ = lv_label_create(screen_);
  lv_label_set_text(timeLabel_, "--:--:--");
  lv_obj_align(timeLabel_, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_text_color(timeLabel_, lv_color_hex(0xF4F8FC), 0);
  lv_obj_set_style_text_font(timeLabel_, &lv_font_montserrat_20, 0);

  dateLabel_ = lv_label_create(screen_);
  lv_label_set_text(dateLabel_, "RTC not set");
  lv_obj_align(dateLabel_, LV_ALIGN_TOP_RIGHT, 0, 24);
  lv_obj_set_style_text_color(dateLabel_, lv_color_hex(0x6F7C8D), 0);
  lv_obj_set_style_text_font(dateLabel_, &lv_font_montserrat_16, 0);

  bleLabel_ = lv_label_create(screen_);
  lv_label_set_text(bleLabel_, LV_SYMBOL_BLUETOOTH);
  lv_obj_align(bleLabel_, LV_ALIGN_TOP_LEFT, 238, 4);
  lv_obj_set_style_text_color(bleLabel_, lv_color_hex(0x566070), 0);
  lv_obj_set_style_text_font(bleLabel_, &lv_font_montserrat_20, 0);

  batteryLabel_ = lv_label_create(screen_);
  lv_label_set_text(batteryLabel_, LV_SYMBOL_BATTERY_FULL " 0%");
  lv_obj_align(batteryLabel_, LV_ALIGN_TOP_LEFT, 266, 6);
  lv_obj_set_style_text_color(batteryLabel_, lv_color_hex(0xD9DEE5), 0);
  lv_obj_set_style_text_font(batteryLabel_, &lv_font_montserrat_16, 0);
}

void UiManager::createNavigation() {
  navDashboard_ = createNavButton(screen_, "Today");
  navTrends_ = createNavButton(screen_, "Trends");
  navDevice_ = createNavButton(screen_, "Device");
  lv_obj_align(navDashboard_, LV_ALIGN_TOP_LEFT, 0, 52);
  lv_obj_align(navTrends_, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_align(navDevice_, LV_ALIGN_TOP_RIGHT, 0, 52);

  lv_obj_add_event_cb(navDashboard_, [](lv_event_t *event) {
    static_cast<UiManager *>(lv_event_get_user_data(event))->setPage(0);
  }, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(navTrends_, [](lv_event_t *event) {
    static_cast<UiManager *>(lv_event_get_user_data(event))->setPage(1);
  }, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(navDevice_, [](lv_event_t *event) {
    static_cast<UiManager *>(lv_event_get_user_data(event))->setPage(2);
  }, LV_EVENT_CLICKED, this);
}

void UiManager::createDashboard() {
  pageDashboard_ = lv_obj_create(screen_);
  lv_obj_set_size(pageDashboard_, 336, 340);
  lv_obj_align(pageDashboard_, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_opa(pageDashboard_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pageDashboard_, 0, 0);
  lv_obj_set_style_pad_all(pageDashboard_, 0, 0);
  lv_obj_clear_flag(pageDashboard_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hero = createCard(pageDashboard_, 336, 138);
  lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 0);
  createCardTitle(hero, "Heart Rate", "HR", lv_color_hex(0xFF5A6B));

  hrValueLabel_ = lv_label_create(hero);
  lv_label_set_text(hrValueLabel_, "--");
  lv_obj_align(hrValueLabel_, LV_ALIGN_LEFT_MID, 0, 16);
  lv_obj_set_style_text_color(hrValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(hrValueLabel_, &lv_font_montserrat_48, 0);

  heartLabel_ = lv_label_create(hero);
  lv_label_set_text(heartLabel_, "bpm");
  lv_obj_align_to(heartLabel_, hrValueLabel_, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -8);
  lv_obj_set_style_text_color(heartLabel_, lv_color_hex(0x7B8796), 0);
  lv_obj_set_style_text_font(heartLabel_, &lv_font_montserrat_20, 0);

  heroHrTrendChart_ = createTrendChart(hero, lv_color_hex(0xFF5A6B), &heroHrSeries_, 45, 180);
  lv_obj_set_size(heroHrTrendChart_, 132, 58);
  lv_obj_align(heroHrTrendChart_, LV_ALIGN_RIGHT_MID, 0, 18);

  lv_obj_t *spo2Card = createCard(pageDashboard_, 104, 122);
  lv_obj_align(spo2Card, LV_ALIGN_TOP_LEFT, 0, 154);
  createCardTitle(spo2Card, "SpO2", LV_SYMBOL_OK, lv_color_hex(0x41C7F5));
  spo2ValueLabel_ = lv_label_create(spo2Card);
  lv_label_set_text(spo2ValueLabel_, "--%");
  lv_obj_align(spo2ValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_set_style_text_color(spo2ValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(spo2ValueLabel_, &lv_font_montserrat_28, 0);

  lv_obj_t *rriCard = createCard(pageDashboard_, 104, 122);
  lv_obj_align(rriCard, LV_ALIGN_TOP_MID, 0, 154);
  createCardTitle(rriCard, "RRI", LV_SYMBOL_LOOP, lv_color_hex(0xFFC857));
  rriValueLabel_ = lv_label_create(rriCard);
  lv_label_set_text(rriValueLabel_, "--");
  lv_obj_align(rriValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_set_style_text_color(rriValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(rriValueLabel_, &lv_font_montserrat_28, 0);

  lv_obj_t *hrvCard = createCard(pageDashboard_, 104, 122);
  lv_obj_align(hrvCard, LV_ALIGN_TOP_RIGHT, 0, 154);
  createCardTitle(hrvCard, "HRV", LV_SYMBOL_CHARGE, lv_color_hex(0x5EE27A));
  hrvValueLabel_ = lv_label_create(hrvCard);
  lv_label_set_text(hrvValueLabel_, "--");
  lv_obj_align(hrvValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_set_style_text_color(hrvValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(hrvValueLabel_, &lv_font_montserrat_28, 0);

  statusLabel_ = lv_label_create(pageDashboard_);
  lv_label_set_text(statusLabel_, "Booting sensor...");
  lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(statusLabel_, 320);
  lv_obj_align(statusLabel_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x8E99A8), 0);
  lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_16, 0);
}

void UiManager::createTrends() {
  pageTrends_ = lv_obj_create(screen_);
  lv_obj_set_size(pageTrends_, 336, 340);
  lv_obj_align(pageTrends_, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_opa(pageTrends_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pageTrends_, 0, 0);
  lv_obj_set_style_pad_all(pageTrends_, 0, 0);
  lv_obj_clear_flag(pageTrends_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cards[4];
  const char *titles[4] = {"Heart Rate", "SpO2", "R-R Interval", "HRV"};
  const char *icons[4] = {"HR", LV_SYMBOL_OK, LV_SYMBOL_LOOP, LV_SYMBOL_CHARGE};
  const uint32_t colors[4] = {0xFF5A6B, 0x41C7F5, 0xFFC857, 0x5EE27A};
  for (uint8_t i = 0; i < 4; ++i) {
    cards[i] = createCard(pageTrends_, 158, 148);
    lv_obj_align(cards[i], (i % 2) == 0 ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                 0, (i / 2) * 164);
    createCardTitle(cards[i], titles[i], icons[i], lv_color_hex(colors[i]));
  }
  hrTrendChart_ = createTrendChart(cards[0], lv_color_hex(0xFF5A6B), &hrSeries_, 45, 180);
  spo2TrendChart_ = createTrendChart(cards[1], lv_color_hex(0x41C7F5), &spo2Series_, 85, 100);
  rriTrendChart_ = createTrendChart(cards[2], lv_color_hex(0xFFC857), &rriSeries_, 300, 1400);
  hrvTrendChart_ = createTrendChart(cards[3], lv_color_hex(0x5EE27A), &hrvSeries_, 0, 180);
}

void UiManager::createDevicePage() {
  pageDevice_ = lv_obj_create(screen_);
  lv_obj_set_size(pageDevice_, 336, 340);
  lv_obj_align(pageDevice_, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_opa(pageDevice_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pageDevice_, 0, 0);
  lv_obj_set_style_pad_all(pageDevice_, 0, 0);
  lv_obj_clear_flag(pageDevice_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *deviceCard = createCard(pageDevice_, 336, 154);
  lv_obj_align(deviceCard, LV_ALIGN_TOP_MID, 0, 0);
  createCardTitle(deviceCard, "Device", LV_SYMBOL_SETTINGS, lv_color_hex(0x41C7F5));
  deviceInfoLabel_ = lv_label_create(deviceCard);
  lv_label_set_text(deviceInfoLabel_, "Waiting for telemetry...");
  lv_label_set_long_mode(deviceInfoLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(deviceInfoLabel_, 306);
  lv_obj_align(deviceInfoLabel_, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_style_text_color(deviceInfoLabel_, lv_color_hex(0xDDE6F3), 0);
  lv_obj_set_style_text_font(deviceInfoLabel_, &lv_font_montserrat_16, 0);

  lv_obj_t *rtcCard = createCard(pageDevice_, 336, 166);
  lv_obj_align(rtcCard, LV_ALIGN_BOTTOM_MID, 0, 0);
  createCardTitle(rtcCard, "Set RTC", LV_SYMBOL_EDIT, lv_color_hex(0xFFC857));
  rtcHelpLabel_ = lv_label_create(rtcCard);
  lv_label_set_text(rtcHelpLabel_,
                    "Serial command:\nRTC=YYYY-MM-DD HH:MM:SS\n\nExample:\nRTC=2026-05-30 14:05:00");
  lv_label_set_long_mode(rtcHelpLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(rtcHelpLabel_, 306);
  lv_obj_align(rtcHelpLabel_, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_style_text_color(rtcHelpLabel_, lv_color_hex(0xDDE6F3), 0);
  lv_obj_set_style_text_font(rtcHelpLabel_, &lv_font_montserrat_16, 0);
}

void UiManager::tick(const VitalData &data, bool bleConnected,
                     uint8_t batteryPercent, const RtcSnapshot &rtc) {
  if (!initialized_) {
    return;
  }

  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - lastLvTickMs_;
  if (elapsedMs > 0U) {
    lv_tick_inc(elapsedMs);
    lastLvTickMs_ = nowMs;
  }

  lv_timer_handler();

  if (!displayOn_) {
    return;
  }

  if ((nowMs - lastRefreshMs_) < cfg::kUiRefreshPeriodMs) {
    return;
  }
  lastRefreshMs_ = nowMs;

  updateUi(data, bleConnected, batteryPercent, rtc);

  const bool pulseState = ((nowMs / 500U) % 2U) == 0U;
  lv_obj_set_style_text_color(heartLabel_,
                              pulseState ? lv_color_hex(0xFF6B7C)
                                         : lv_color_hex(0x9F283C),
                              0);
}

void UiManager::updateUi(const VitalData &data, bool bleConnected,
                         uint8_t batteryPercent, const RtcSnapshot &rtc) {
  if (lastSnapshot_.rtcValid != rtc.valid ||
      strcmp(lastSnapshot_.timeText, rtc.timeText) != 0 ||
      strcmp(lastSnapshot_.dateText, rtc.dateText) != 0) {
    lv_label_set_text(timeLabel_, rtc.valid ? rtc.timeText : "--:--:--");
    lv_label_set_text(dateLabel_, rtc.valid ? rtc.dateText : "RTC not set");
    lastSnapshot_.rtcValid = rtc.valid;
    strncpy(lastSnapshot_.timeText, rtc.timeText, sizeof(lastSnapshot_.timeText));
    strncpy(lastSnapshot_.dateText, rtc.dateText, sizeof(lastSnapshot_.dateText));
  }

  if (lastSnapshot_.bleConnected != bleConnected) {
    lv_obj_set_style_text_color(bleLabel_,
                                bleConnected ? lv_color_hex(0x44C5FF)
                                             : lv_color_hex(0x566070),
                                0);
    lastSnapshot_.bleConnected = bleConnected;
  }

  if (lastSnapshot_.batteryPercent != batteryPercent) {
    char batteryText[24];
    snprintf(batteryText, sizeof(batteryText), "%s %u%%",
             batteryPercent > 65   ? LV_SYMBOL_BATTERY_FULL
             : batteryPercent > 30 ? LV_SYMBOL_BATTERY_2
                                   : LV_SYMBOL_BATTERY_1,
             batteryPercent);
    lv_label_set_text(batteryLabel_, batteryText);
    lastSnapshot_.batteryPercent = batteryPercent;
  }

  if (memcmp(&lastSnapshot_.data, &data, sizeof(VitalData)) == 0) {
    return;
  }

  const bool vitalsValid = (data.status & cfg::kStatusVitalsValid) != 0U;
  char hrText[16];
  snprintf(hrText, sizeof(hrText), vitalsValid && data.hr > 0 ? "%u" : "--", data.hr);
  lv_label_set_text(hrValueLabel_, hrText);

  if (vitalsValid && data.spo2_x100 > 0U) {
    char text[20];
    snprintf(text, sizeof(text), "%u%%", data.spo2_x100 / 100U);
    lv_label_set_text(spo2ValueLabel_, text);
  } else {
    lv_label_set_text(spo2ValueLabel_, "--%");
  }

  setMetricValue(rriValueLabel_, "ms", data.rri,
                 (data.status & cfg::kStatusRriValid) != 0U);
  setMetricValue(hrvValueLabel_, "ms", data.hrv,
                 (data.status & cfg::kStatusHrvValid) != 0U);
  updateStatusText(data);
  updateTrendCharts(data);

  char deviceText[160];
  snprintf(deviceText, sizeof(deviceText),
           "Battery: %u%%\nBLE: %s\nRTC: %s\nVitals: %s",
           batteryPercent, bleConnected ? "connected" : "advertising",
           rtc.available ? (rtc.valid ? "running" : "needs set") : "offline",
           vitalsValid ? "streaming" : "waiting");
  lv_label_set_text(deviceInfoLabel_, deviceText);

  lastSnapshot_.data = data;
}

void UiManager::updateStatusText(const VitalData &data) {
  if ((data.status & cfg::kStatusSensorError) != 0U) {
    lv_label_set_text(statusLabel_, "Sensor offline: cek VCC, GND, SDA(15), SCL(14)");
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFF6B6B), 0);
    return;
  }

  if ((data.status & cfg::kStatusVitalsValid) == 0U) {
    lv_label_set_text(statusLabel_, "Place finger on MAX30102 and hold still");
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFFC857), 0);
    return;
  }

  lv_label_set_text(statusLabel_, "Sensor streaming");
  lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x5EE27A), 0);
}

void UiManager::setMetricValue(lv_obj_t *label, const char *suffix, uint16_t value,
                               bool valid) {
  char text[24];
  if (valid && value > 0U) {
    snprintf(text, sizeof(text), "%u %s", value, suffix);
  } else {
    snprintf(text, sizeof(text), "-- %s", suffix);
  }
  lv_label_set_text(label, text);
}

void UiManager::updateTrendCharts(const VitalData &data) {
  const bool vitalsValid = (data.status & cfg::kStatusVitalsValid) != 0U;
  const bool rriValid = (data.status & cfg::kStatusRriValid) != 0U;
  const bool hrvValid = (data.status & cfg::kStatusHrvValid) != 0U;
  lv_chart_set_next_value(hrTrendChart_, hrSeries_, vitalsValid ? data.hr : 0);
  lv_chart_set_next_value(heroHrTrendChart_, heroHrSeries_, vitalsValid ? data.hr : 0);
  lv_chart_set_next_value(spo2TrendChart_, spo2Series_,
                          vitalsValid ? data.spo2_x100 / 100U : 0);
  lv_chart_set_next_value(rriTrendChart_, rriSeries_, rriValid ? data.rri : 0);
  lv_chart_set_next_value(hrvTrendChart_, hrvSeries_, hrvValid ? data.hrv : 0);
  ++trendCount_;
}

void UiManager::setPage(uint8_t page) {
  activePage_ = page;
  if (page == 0) {
    lv_obj_clear_flag(pageDashboard_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pageDashboard_, LV_OBJ_FLAG_HIDDEN);
  }
  if (page == 1) {
    lv_obj_clear_flag(pageTrends_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pageTrends_, LV_OBJ_FLAG_HIDDEN);
  }
  if (page == 2) {
    lv_obj_clear_flag(pageDevice_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pageDevice_, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_bg_color(navDashboard_, lv_color_hex(page == 0 ? 0x159BDE : 0x111722), 0);
  lv_obj_set_style_bg_color(navTrends_, lv_color_hex(page == 1 ? 0x159BDE : 0x111722), 0);
  lv_obj_set_style_bg_color(navDevice_, lv_color_hex(page == 2 ? 0x159BDE : 0x111722), 0);
}

void UiManager::setDisplayOn(bool on) {
  displayOn_ = on;
  if (g_gfx != nullptr) {
    static_cast<Arduino_SH8601 *>(g_gfx)->setBrightness(on ? 220 : 0);
  }
}

void UiManager::toggleDisplay() { setDisplayOn(!displayOn_); }

bool UiManager::displayOn() const { return displayOn_; }
