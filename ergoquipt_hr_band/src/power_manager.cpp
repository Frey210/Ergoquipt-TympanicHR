#include "power_manager.h"

#include <Wire.h>

namespace {

constexpr uint8_t kTcaInputReg = 0x00;
constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;
constexpr uint32_t kBootDebounceMs = 60;

}  // namespace

void PowerManager::begin() {
  pinMode(cfg::kBootButtonPin, INPUT_PULLUP);
  expanderReady_ = initExpander();
  pmuReady_ = initPmu();
  updateBattery(millis());

  Serial.printf("Power: AXP2101=%s TCA9554=0x%02X %s battery=%u%%\n",
                pmuReady_ ? "ok" : "missing", cfg::kTca9554Address,
                expanderReady_ ? "ok" : "missing", batteryPercent_);
}

bool PowerManager::initExpander() {
  uint8_t config = 0;
  uint8_t output = 0;
  if (!expanderReadReg(kTcaConfigReg, config) ||
      !expanderReadReg(kTcaOutputReg, output)) {
    return false;
  }

  expanderConfig_ = config;
  expanderOutput_ = output;
  expanderPinMode(cfg::kExpanderPmuIrqPin, true);
  expanderPinMode(cfg::kExpanderPowerButtonPin, true);
  expanderPinMode(cfg::kExpanderPowerHoldPin1, false);
  expanderPinMode(cfg::kExpanderPowerHoldPin2, false);
  expanderDigitalWrite(cfg::kExpanderPowerHoldPin1, false);
  expanderDigitalWrite(cfg::kExpanderPowerHoldPin2, false);
  delay(20);
  expanderDigitalWrite(cfg::kExpanderPowerHoldPin1, true);
  expanderDigitalWrite(cfg::kExpanderPowerHoldPin2, true);
  return true;
}

bool PowerManager::initPmu() {
  bool ok = false;
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  ok = power_.begin(Wire, cfg::kAxp2101Address, cfg::kI2cSdaPin,
                    cfg::kI2cSclPin);
  if (ok) {
    power_.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power_.clearIrqStatus();
    power_.enableBattDetection();
    power_.enableBattVoltageMeasure();
    power_.enableVbusVoltageMeasure();
    power_.enableSystemVoltageMeasure();
    power_.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                     XPOWERS_AXP2101_PKEY_LONG_IRQ);
    power_.setLongPressPowerOFF();
    power_.enableLongPressShutdown();
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
  return ok;
}

void PowerManager::poll() {
  const uint32_t nowMs = millis();
  pollPmuIrq();
  pollBootButton(nowMs);
  if ((nowMs - lastBatteryPollMs_) >= cfg::kBatteryPollPeriodMs) {
    updateBattery(nowMs);
  }
}

void PowerManager::pollPmuIrq() {
  if (!pmuReady_) {
    return;
  }

  bool irqAsserted = true;
  if (expanderReady_ &&
      !expanderDigitalRead(cfg::kExpanderPmuIrqPin, irqAsserted)) {
    irqAsserted = true;
  }
  if (expanderReady_ && !irqAsserted) {
    return;
  }

  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  power_.getIrqStatus();
  if (power_.isPekeyShortPressIrq()) {
    shortPressPending_ = true;
  }
  if (power_.isPekeyLongPressIrq()) {
    longPressPending_ = true;
  }
  power_.clearIrqStatus();
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
}

void PowerManager::pollBootButton(uint32_t nowMs) {
  const bool pressed = digitalRead(cfg::kBootButtonPin) == LOW;
  if (pressed != bootWasPressed_ && (nowMs - lastBootEdgeMs_) >= kBootDebounceMs) {
    lastBootEdgeMs_ = nowMs;
    bootWasPressed_ = pressed;
    if (!pressed) {
      bootPressPending_ = true;
    }
  }
}

void PowerManager::updateBattery(uint32_t nowMs) {
  lastBatteryPollMs_ = nowMs;
  if (!pmuReady_) {
    return;
  }

  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  if (power_.isBatteryConnect()) {
    batteryPercent_ = static_cast<uint8_t>(constrain(power_.getBatteryPercent(), 0, 100));
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
}

uint8_t PowerManager::batteryPercent() const { return batteryPercent_; }

bool PowerManager::available() const { return pmuReady_; }

bool PowerManager::takeShortPress() {
  const bool pending = shortPressPending_;
  shortPressPending_ = false;
  return pending;
}

bool PowerManager::takeLongPress() {
  const bool pending = longPressPending_;
  longPressPending_ = false;
  return pending;
}

bool PowerManager::takeBootPress() {
  const bool pending = bootPressPending_;
  bootPressPending_ = false;
  return pending;
}

void PowerManager::shutdown() {
  if (!pmuReady_) {
    return;
  }
  if (g_i2cMutex != nullptr) {
    xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  }
  power_.shutdown();
  if (g_i2cMutex != nullptr) {
    xSemaphoreGive(g_i2cMutex);
  }
}

bool PowerManager::expanderPinMode(uint8_t pin, bool input) {
  if (pin > 7U) {
    return false;
  }
  if (input) {
    expanderConfig_ |= (1U << pin);
  } else {
    expanderConfig_ &= ~(1U << pin);
  }
  return expanderWriteReg(kTcaConfigReg, expanderConfig_);
}

bool PowerManager::expanderDigitalWrite(uint8_t pin, bool high) {
  if (pin > 7U) {
    return false;
  }
  if (high) {
    expanderOutput_ |= (1U << pin);
  } else {
    expanderOutput_ &= ~(1U << pin);
  }
  return expanderWriteReg(kTcaOutputReg, expanderOutput_);
}

bool PowerManager::expanderDigitalRead(uint8_t pin, bool &high) {
  if (pin > 7U) {
    return false;
  }
  uint8_t value = 0;
  if (!expanderReadReg(kTcaInputReg, value)) {
    return false;
  }
  high = (value & (1U << pin)) != 0U;
  return true;
}

bool PowerManager::expanderReadReg(uint8_t reg, uint8_t &value) {
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

bool PowerManager::expanderWriteReg(uint8_t reg, uint8_t value) {
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
