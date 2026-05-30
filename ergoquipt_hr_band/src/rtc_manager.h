#pragma once

#include <Arduino.h>
#include <SensorPCF85063.hpp>

#include "config.h"

struct RtcSnapshot {
  bool available = false;
  bool valid = false;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  char timeText[9] = "--:--:--";
  char dateText[11] = "----/--/--";
};

class RtcManager {
 public:
  void begin();
  void poll();
  RtcSnapshot snapshot() const;
  bool setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second);
  bool available() const;

 private:
  bool parseSerialCommand(const String &line);
  bool parseDateTime(const char *text, uint16_t &year, uint8_t &month,
                     uint8_t &day, uint8_t &hour, uint8_t &minute,
                     uint8_t &second) const;
  void updateSnapshot(uint32_t nowMs);

  SensorPCF85063 rtc_;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  RtcSnapshot snapshot_{};
  String serialBuffer_;
  uint32_t lastPollMs_ = 0;
  bool available_ = false;
};
