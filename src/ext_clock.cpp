#include "ext_clock.h"

#include <Wire.h>
#include <math.h>

ExtClock gExtClock;

#if EXT_RTC_EN

namespace {

constexpr uint8_t kDs3231Addr = EXT_RTC_I2C_ADDR;
constexpr uint8_t kDs3231RegTempMsb = 0x11;

bool i2cWriteRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t got = Wire.requestFrom(static_cast<int>(addr), static_cast<int>(n));
  if (got != n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    buf[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

}  // namespace

bool ExtClock::probeDs3231() {
  uint8_t raw[2] = {};
  return i2cWriteRead(kDs3231Addr, kDs3231RegTempMsb, raw, 2);
}

bool ExtClock::readDs3231Temp(float* outC) {
  uint8_t raw[2] = {};
  if (!i2cWriteRead(kDs3231Addr, kDs3231RegTempMsb, raw, 2)) {
    return false;
  }
  // DS3231: temp = (int8)MSB + LSB[7:6] * 0.25
  const int8_t whole = static_cast<int8_t>(raw[0]);
  const float frac = static_cast<float>((raw[1] >> 6) & 0x3) * 0.25f;
  *outC = static_cast<float>(whole) + frac;
  return true;
}

void ExtClock::begin() {
  enabled_ = true;
  driver_ = "ds3231";
  ppmFloorHint_ = EXT_RTC_PPM_FLOOR;
  // Wire is owned by OLED (DisplayUi::begin). Probe once; soft-fail if absent.
  if (probeDs3231()) {
    healthy_ = true;
    reason_ = "ok";
    float t = 0;
    if (readDs3231Temp(&t)) {
      tempC_ = t;
      haveTemp_ = true;
    }
    Serial.printf("[extclk] DS3231 @0x%02X healthy (holdover floor %.1f ppm)\n",
                  static_cast<unsigned>(kDs3231Addr),
                  static_cast<double>(EXT_RTC_PPM_FLOOR));
  } else {
    healthy_ = false;
    reason_ = "no device";
    Serial.printf("[extclk] DS3231 @0x%02X not found — assist off\n",
                  static_cast<unsigned>(kDs3231Addr));
  }
}

void ExtClock::poll() {
  if (!enabled_) {
    return;
  }
  const uint32_t now = millis();
  if (lastPollMs_ != 0 && (now - lastPollMs_) < 2000u) {
    return;
  }
  lastPollMs_ = now;

  float t = 0;
  if (readDs3231Temp(&t)) {
    tempC_ = t;
    haveTemp_ = true;
    failStreak_ = 0;
    if (!healthy_) {
      healthy_ = true;
      reason_ = "ok";
      Serial.println("[extclk] DS3231 recovered");
    }
  } else {
    if (failStreak_ < 250) {
      failStreak_++;
    }
    if (failStreak_ >= 3) {
      if (healthy_) {
        Serial.println("[extclk] DS3231 lost");
      }
      healthy_ = false;
      reason_ = "i2c fail";
    }
  }
}

#else  // !EXT_RTC_EN

bool ExtClock::probeDs3231() {
  return false;
}

bool ExtClock::readDs3231Temp(float* /*outC*/) {
  return false;
}

void ExtClock::begin() {
  enabled_ = false;
  healthy_ = false;
  driver_ = "none";
  reason_ = "EXT_RTC_EN=0";
  ppmFloorHint_ = NAN;
  Serial.println("[extclk] disabled (EXT_RTC_EN=0)");
}

void ExtClock::poll() {}

#endif
