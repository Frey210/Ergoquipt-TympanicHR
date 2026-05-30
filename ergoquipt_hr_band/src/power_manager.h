#pragma once

#include <Arduino.h>
#include <XPowersLib.h>

#include "config.h"

class PowerManager {
 public:
  void begin();
  void poll();
  uint8_t batteryPercent() const;
  bool available() const;
  bool takeShortPress();
  bool takeLongPress();
  bool takeBootPress();
  void shutdown();

 private:
  bool initExpander();
  bool initPmu();
  bool expanderPinMode(uint8_t pin, bool input);
  bool expanderDigitalWrite(uint8_t pin, bool high);
  bool expanderDigitalRead(uint8_t pin, bool &high);
  bool expanderReadReg(uint8_t reg, uint8_t &value);
  bool expanderWriteReg(uint8_t reg, uint8_t value);
  void updateBattery(uint32_t nowMs);
  void pollPmuIrq();
  void pollBootButton(uint32_t nowMs);

  XPowersPMU power_;
  uint8_t expanderConfig_ = 0xFF;
  uint8_t expanderOutput_ = 0x00;
  uint8_t batteryPercent_ = cfg::kMockBatteryStartPct;
  uint32_t lastBatteryPollMs_ = 0;
  uint32_t lastBootEdgeMs_ = 0;
  bool pmuReady_ = false;
  bool expanderReady_ = false;
  bool bootWasPressed_ = false;
  bool shortPressPending_ = false;
  bool longPressPending_ = false;
  bool bootPressPending_ = false;
};
