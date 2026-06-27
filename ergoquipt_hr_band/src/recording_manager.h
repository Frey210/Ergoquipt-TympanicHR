#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include "config.h"
#include "rtc_manager.h"

struct RecordingSnapshot {
  bool sdReady = false;
  bool recording = false;
  uint32_t rowsWritten = 0;
  uint64_t cardSizeMb = 0;
  char fileName[40] = "";
  char statusText[96] = "Recorder idle";
};

class RecordingManager {
 public:
  void begin();
  bool start(const RtcSnapshot &rtc);
  void stop();
  void append(const VitalData &data, uint8_t batteryPercent, bool bleConnected,
              const RtcSnapshot &rtc, FilteringMode mode,
              const SensorDiagnostics &diagnostics);
  RecordingSnapshot snapshot() const;
  bool recording() const;
  bool sdReady() const;

 private:
  bool enableSdSlot();
  bool expanderReadReg(uint8_t reg, uint8_t &value);
  bool expanderWriteReg(uint8_t reg, uint8_t value);
  void setStatus(const char *status);
  void buildFileName(const RtcSnapshot &rtc, char *out, size_t outSize) const;

  File file_;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  RecordingSnapshot snapshot_{};
  bool mounted_ = false;
};
