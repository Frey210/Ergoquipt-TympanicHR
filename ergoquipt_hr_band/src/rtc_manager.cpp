#include "rtc_manager.h"

#include <Wire.h>

namespace {

bool isValidDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second) {
  return year >= 2024 && year <= 2099 && month >= 1 && month <= 12 &&
         day >= 1 && day <= 31 && hour <= 23 && minute <= 59 && second <= 59;
}

}  // namespace

void RtcManager::begin() {
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  available_ = rtc_.begin(Wire, cfg::kPcf85063Address, cfg::kI2cSdaPin,
                          cfg::kI2cSclPin);
  if (available_) {
    rtc_.start();
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  updateSnapshot(millis());
  Serial.printf("RTC: PCF85063 %s on I2C address 0x%02X\n",
                available_ ? "initialized" : "not detected",
                cfg::kPcf85063Address);
  Serial.println("RTC command: RTC=YYYY-MM-DD HH:MM:SS");
}

void RtcManager::poll() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (serialBuffer_.length() > 0) {
        parseSerialCommand(serialBuffer_);
        serialBuffer_ = "";
      }
    } else if (serialBuffer_.length() < 40) {
      serialBuffer_ += ch;
    }
  }

  const uint32_t nowMs = millis();
  if ((nowMs - lastPollMs_) >= cfg::kRtcPollPeriodMs) {
    updateSnapshot(nowMs);
  }
}

void RtcManager::updateSnapshot(uint32_t nowMs) {
  lastPollMs_ = nowMs;
  RtcSnapshot updated;
  updated.available = available_;
  if (!available_) {
    portENTER_CRITICAL(&dataMux_);
    snapshot_ = updated;
    portEXIT_CRITICAL(&dataMux_);
    return;
  }

  RTC_DateTime datetime;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  datetime = rtc_.getDateTime();
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }

  updated.valid = datetime.available &&
                  isValidDateTime(datetime.year, datetime.month, datetime.day,
                                  datetime.hour, datetime.minute,
                                  datetime.second);
  updated.year = datetime.year;
  updated.month = datetime.month;
  updated.day = datetime.day;
  updated.hour = datetime.hour;
  updated.minute = datetime.minute;
  updated.second = datetime.second;
  if (updated.valid) {
    snprintf(updated.timeText, sizeof(updated.timeText), "%02u:%02u:%02u",
             updated.hour, updated.minute, updated.second);
    snprintf(updated.dateText, sizeof(updated.dateText), "%04u/%02u/%02u",
             updated.year, updated.month, updated.day);
  }

  portENTER_CRITICAL(&dataMux_);
  snapshot_ = updated;
  portEXIT_CRITICAL(&dataMux_);
}

RtcSnapshot RtcManager::snapshot() const {
  RtcSnapshot copy;
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  copy = snapshot_;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return copy;
}

bool RtcManager::setDateTime(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second) {
  if (!available_ ||
      !isValidDateTime(year, month, day, hour, minute, second)) {
    return false;
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  rtc_.setDateTime(year, month, day, hour, minute, second);
  rtc_.start();
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  updateSnapshot(millis());
  return true;
}

bool RtcManager::parseSerialCommand(const String &line) {
  String command = line;
  command.trim();
  if (!command.startsWith("RTC=") && !command.startsWith("TIME=")) {
    return false;
  }

  const int valueStart = command.indexOf('=') + 1;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  if (!parseDateTime(command.c_str() + valueStart, year, month, day, hour,
                     minute, second)) {
    Serial.println("RTC set failed: use RTC=YYYY-MM-DD HH:MM:SS");
    return false;
  }

  const bool ok = setDateTime(year, month, day, hour, minute, second);
  Serial.println(ok ? "RTC set OK" : "RTC set failed: PCF85063 unavailable");
  return ok;
}

bool RtcManager::parseDateTime(const char *text, uint16_t &year, uint8_t &month,
                               uint8_t &day, uint8_t &hour, uint8_t &minute,
                               uint8_t &second) const {
  unsigned parsedYear = 0;
  unsigned parsedMonth = 0;
  unsigned parsedDay = 0;
  unsigned parsedHour = 0;
  unsigned parsedMinute = 0;
  unsigned parsedSecond = 0;
  if (sscanf(text, "%u-%u-%u %u:%u:%u", &parsedYear, &parsedMonth, &parsedDay,
             &parsedHour, &parsedMinute, &parsedSecond) != 6) {
    return false;
  }
  year = static_cast<uint16_t>(parsedYear);
  month = static_cast<uint8_t>(parsedMonth);
  day = static_cast<uint8_t>(parsedDay);
  hour = static_cast<uint8_t>(parsedHour);
  minute = static_cast<uint8_t>(parsedMinute);
  second = static_cast<uint8_t>(parsedSecond);
  return isValidDateTime(year, month, day, hour, minute, second);
}

bool RtcManager::available() const { return available_; }
