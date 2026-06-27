#include "recording_manager.h"

#include <Wire.h>

namespace {

constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;

void copyText(char *dest, size_t destSize, const char *src) {
  if (destSize == 0) {
    return;
  }
  strncpy(dest, src, destSize - 1U);
  dest[destSize - 1U] = '\0';
}

const char *filteringModeName(FilteringMode mode) {
  switch (mode) {
    case FilteringMode::M0NoImu:
      return "M0";
    case FilteringMode::M1MotionGating:
      return "M1";
    case FilteringMode::M2MotionAdaptive:
      return "M2";
    case FilteringMode::M3AdaptiveNoise:
      return "M3";
    default:
      return "M?";
  }
}

const char *motionStateName(uint8_t state) {
  switch (state) {
    case 0:
      return "stable";
    case 1:
      return "moderate";
    case 2:
      return "high";
    default:
      return "unknown";
  }
}

}  // namespace

void RecordingManager::begin() {
  if (!enableSdSlot()) {
    setStatus("SD enable failed");
    Serial.println("Recorder: SD EXIO7 enable failed");
    return;
  }

  SD_MMC.setPins(cfg::kSdClkPin, cfg::kSdCmdPin, cfg::kSdDataPin);
  mounted_ = SD_MMC.begin("/sdcard", true);
  if (!mounted_ || SD_MMC.cardType() == CARD_NONE) {
    mounted_ = false;
    setStatus("SD card not mounted");
    Serial.println("Recorder: SD card not mounted");
    return;
  }

  portENTER_CRITICAL(&dataMux_);
  snapshot_.sdReady = true;
  snapshot_.cardSizeMb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  copyText(snapshot_.statusText, sizeof(snapshot_.statusText), "SD ready");
  portEXIT_CRITICAL(&dataMux_);

  Serial.printf("Recorder: SD ready, size=%lluMB\n", snapshot_.cardSizeMb);
}

bool RecordingManager::start(const RtcSnapshot &rtc) {
  if (!mounted_) {
    setStatus("Insert SD card");
    return false;
  }
  if (file_) {
    file_.close();
  }

  char fileName[40];
  buildFileName(rtc, fileName, sizeof(fileName));
  file_ = SD_MMC.open(fileName, FILE_WRITE);
  if (!file_) {
    setStatus("Open CSV failed");
    Serial.printf("Recorder: failed to open %s\n", fileName);
    return false;
  }

  file_.println(
      "millis,date,time,filter_mode,hr,spo2_x100,rri,hrv,status,battery_pct,"
      "ble_connected,ir_raw,red_raw,ir_filtered,acc_x,acc_y,acc_z,acc_mag,"
      "motion_score,motion_state,imu_ready,finger_present,peak_detected,rri_accepted");
  file_.flush();

  portENTER_CRITICAL(&dataMux_);
  snapshot_.recording = true;
  snapshot_.rowsWritten = 0;
  copyText(snapshot_.fileName, sizeof(snapshot_.fileName), fileName);
  copyText(snapshot_.statusText, sizeof(snapshot_.statusText), "Recording");
  portEXIT_CRITICAL(&dataMux_);

  Serial.printf("Recorder: started %s\n", fileName);
  return true;
}

void RecordingManager::stop() {
  if (file_) {
    file_.flush();
    file_.close();
  }

  portENTER_CRITICAL(&dataMux_);
  snapshot_.recording = false;
  copyText(snapshot_.statusText, sizeof(snapshot_.statusText), "Recording stopped");
  portEXIT_CRITICAL(&dataMux_);

  Serial.println("Recorder: stopped");
}

void RecordingManager::append(const VitalData &data, uint8_t batteryPercent,
                              bool bleConnected, const RtcSnapshot &rtc,
                              FilteringMode mode,
                              const SensorDiagnostics &diagnostics) {
  if (!file_ || !recording()) {
    return;
  }

  file_.printf("%lu,%s,%s,%s,%u,%u,%u,%u,0x%02X,%u,%u,%lu,%lu,%lu,%.4f,%.4f,%.4f,%.4f,%.5f,%s,%u,%u,%u,%u\n",
               static_cast<unsigned long>(millis()),
               rtc.valid ? rtc.dateText : "",
               rtc.valid ? rtc.timeText : "",
               filteringModeName(mode),
               data.hr, data.spo2_x100, data.rri, data.hrv, data.status,
               batteryPercent, bleConnected ? 1U : 0U,
               static_cast<unsigned long>(diagnostics.irRaw),
               static_cast<unsigned long>(diagnostics.redRaw),
               static_cast<unsigned long>(diagnostics.irFiltered),
               static_cast<double>(diagnostics.accelX),
               static_cast<double>(diagnostics.accelY),
               static_cast<double>(diagnostics.accelZ),
               static_cast<double>(diagnostics.accelMagnitude),
               static_cast<double>(diagnostics.motionScore),
               motionStateName(diagnostics.motionState),
               diagnostics.imuReady ? 1U : 0U,
               diagnostics.fingerPresent ? 1U : 0U,
               diagnostics.peakDetected ? 1U : 0U,
               diagnostics.rriAccepted ? 1U : 0U);
  file_.flush();

  portENTER_CRITICAL(&dataMux_);
  ++snapshot_.rowsWritten;
  portEXIT_CRITICAL(&dataMux_);
}

RecordingSnapshot RecordingManager::snapshot() const {
  RecordingSnapshot copy;
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  copy = snapshot_;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return copy;
}

bool RecordingManager::recording() const {
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  const bool value = snapshot_.recording;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return value;
}

bool RecordingManager::sdReady() const {
  portENTER_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  const bool value = snapshot_.sdReady;
  portEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&dataMux_));
  return value;
}

bool RecordingManager::enableSdSlot() {
  uint8_t config = 0;
  uint8_t output = 0;
  if (!expanderReadReg(kTcaConfigReg, config) ||
      !expanderReadReg(kTcaOutputReg, output)) {
    return false;
  }

  config &= ~(1U << cfg::kExpanderSdEnablePin);
  output |= (1U << cfg::kExpanderSdEnablePin);
  return expanderWriteReg(kTcaConfigReg, config) &&
         expanderWriteReg(kTcaOutputReg, output);
}

bool RecordingManager::expanderReadReg(uint8_t reg, uint8_t &value) {
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  Wire.beginTransmission(cfg::kTca9554Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) == 0 &&
      Wire.requestFrom(cfg::kTca9554Address, static_cast<uint8_t>(1)) == 1) {
    value = Wire.read();
    ok = true;
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  return ok;
}

bool RecordingManager::expanderWriteReg(uint8_t reg, uint8_t value) {
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  Wire.beginTransmission(cfg::kTca9554Address);
  Wire.write(reg);
  Wire.write(value);
  ok = Wire.endTransmission() == 0;
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  return ok;
}

void RecordingManager::setStatus(const char *status) {
  portENTER_CRITICAL(&dataMux_);
  copyText(snapshot_.statusText, sizeof(snapshot_.statusText), status);
  portEXIT_CRITICAL(&dataMux_);
}

void RecordingManager::buildFileName(const RtcSnapshot &rtc, char *out,
                                     size_t outSize) const {
  if (rtc.valid) {
    snprintf(out, outSize, "/ERGO_%04u%02u%02u_%02u%02u%02u.csv", rtc.year,
             rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
  } else {
    snprintf(out, outSize, "/ERGO_%lu.csv", static_cast<unsigned long>(millis()));
  }
}
