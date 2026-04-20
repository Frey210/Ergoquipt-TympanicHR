#include "ui_manager.h"

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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

lv_obj_t *createMetricCard(lv_obj_t *parent, const char *title, lv_coord_t col,
                           lv_coord_t row) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 156, 120);
  lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH,
                       row, 1);
  lv_obj_set_style_radius(card, 24, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x12151D), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_100, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 18, 0);

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, title);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x8E99A8), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  return card;
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
  lv_obj_set_style_bg_grad_color(screen_, lv_color_hex(0x090B10), 0);
  lv_obj_set_style_bg_grad_dir(screen_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(screen_, 0, 0);
  lv_obj_set_style_pad_all(screen_, 20, 0);

  titleLabel_ = lv_label_create(screen_);
  lv_label_set_text(titleLabel_, "Monitoring");
  lv_obj_align(titleLabel_, LV_ALIGN_TOP_LEFT, 0, 4);
  lv_obj_set_style_text_color(titleLabel_, lv_color_hex(0xF3F5F7), 0);
  lv_obj_set_style_text_font(titleLabel_, &lv_font_montserrat_24, 0);

  heartLabel_ = lv_label_create(screen_);
  lv_label_set_text(heartLabel_, "HR");
  lv_obj_align(heartLabel_, LV_ALIGN_TOP_LEFT, 282, 8);
  lv_obj_set_style_text_color(heartLabel_, lv_color_hex(0xFF5A6B), 0);
  lv_obj_set_style_text_font(heartLabel_, &lv_font_montserrat_22, 0);

  bleLabel_ = lv_label_create(screen_);
  lv_label_set_text(bleLabel_, LV_SYMBOL_BLUETOOTH);
  lv_obj_align(bleLabel_, LV_ALIGN_TOP_RIGHT, -46, 8);
  lv_obj_set_style_text_color(bleLabel_, lv_color_hex(0x566070), 0);
  lv_obj_set_style_text_font(bleLabel_, &lv_font_montserrat_20, 0);

  batteryLabel_ = lv_label_create(screen_);
  lv_label_set_text(batteryLabel_, LV_SYMBOL_BATTERY_FULL " 0%");
  lv_obj_align(batteryLabel_, LV_ALIGN_TOP_RIGHT, 0, 8);
  lv_obj_set_style_text_color(batteryLabel_, lv_color_hex(0xD9DEE5), 0);
  lv_obj_set_style_text_font(batteryLabel_, &lv_font_montserrat_16, 0);

  statusLabel_ = lv_label_create(screen_);
  lv_label_set_text(statusLabel_, "Booting sensor...");
  lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(statusLabel_, 328);
  lv_obj_align(statusLabel_, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x8E99A8), 0);
  lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_16, 0);

  static lv_coord_t columns[] = {156, 156, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t rows[] = {120, 120, LV_GRID_TEMPLATE_LAST};
  lv_obj_t *grid = lv_obj_create(screen_);
  lv_obj_set_size(grid, 328, 270);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid, columns, rows);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_gap(grid, 16, 0);

  lv_obj_t *hrCard = createMetricCard(grid, "Heart Rate", 0, 0);
  hrValueLabel_ = lv_label_create(hrCard);
  lv_label_set_text(hrValueLabel_, "-- bpm");
  lv_obj_align(hrValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_text_color(hrValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(hrValueLabel_, &lv_font_montserrat_34, 0);

  lv_obj_t *spo2Card = createMetricCard(grid, "SpO2", 1, 0);
  spo2ValueLabel_ = lv_label_create(spo2Card);
  lv_label_set_text(spo2ValueLabel_, "--.-- %");
  lv_obj_align(spo2ValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_text_color(spo2ValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(spo2ValueLabel_, &lv_font_montserrat_28, 0);

  lv_obj_t *rriCard = createMetricCard(grid, "R-R Interval", 0, 1);
  rriValueLabel_ = lv_label_create(rriCard);
  lv_label_set_text(rriValueLabel_, "-- ms");
  lv_obj_align(rriValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_text_color(rriValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(rriValueLabel_, &lv_font_montserrat_28, 0);

  lv_obj_t *hrvCard = createMetricCard(grid, "HRV", 1, 1);
  hrvValueLabel_ = lv_label_create(hrvCard);
  lv_label_set_text(hrvValueLabel_, "-- ms");
  lv_obj_align(hrvValueLabel_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_text_color(hrvValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(hrvValueLabel_, &lv_font_montserrat_28, 0);

  lv_scr_load(screen_);
}

void UiManager::tick(const VitalData &data, bool bleConnected, uint8_t batteryPercent) {
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

  if ((nowMs - lastRefreshMs_) < cfg::kUiRefreshPeriodMs) {
    return;
  }
  lastRefreshMs_ = nowMs;

  updateUi(data, bleConnected, batteryPercent);

  const bool pulseState = ((nowMs / 500U) % 2U) == 0U;
  lv_obj_set_style_text_color(heartLabel_,
                              pulseState ? lv_color_hex(0xFF6B7C)
                                         : lv_color_hex(0x9F283C),
                              0);
}

void UiManager::updateUi(const VitalData &data, bool bleConnected,
                         uint8_t batteryPercent) {
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

  setMetricValue(hrValueLabel_, "bpm", data.hr,
                 (data.status & cfg::kStatusVitalsValid) != 0U);

  if ((data.status & cfg::kStatusVitalsValid) != 0U && data.spo2_x100 > 0U) {
    char text[20];
    snprintf(text, sizeof(text), "%u.%02u %%", data.spo2_x100 / 100U,
             data.spo2_x100 % 100U);
    lv_label_set_text(spo2ValueLabel_, text);
  } else {
    lv_label_set_text(spo2ValueLabel_, "--.-- %");
  }

  setMetricValue(rriValueLabel_, "ms", data.rri,
                 (data.status & cfg::kStatusRriValid) != 0U);
  setMetricValue(hrvValueLabel_, "ms", data.hrv,
                 (data.status & cfg::kStatusHrvValid) != 0U);
  updateStatusText(data);

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
