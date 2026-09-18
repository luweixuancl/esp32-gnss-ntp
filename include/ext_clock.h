#pragma once

#include <Arduino.h>
#include "config.h"

// Optional external high-quality clock assist (docs/ext_clock_design.md).
// Default: EXT_RTC_EN=0 → enabled()==false (no I2C traffic). When enabled and
// a DS3231 answers at EXT_RTC_I2C_ADDR, healthy() feeds LocalClock holdover
// dispersion with a tighter ppm floor (does NOT replace GNSS stratum-1).
class ExtClock {
 public:
  void begin();
  // Call from task-time (shares OLED Wire bus when EXT_RTC_EN=1).
  void poll();

  bool enabled() const { return enabled_; }
  bool healthy() const { return healthy_; }
  // Suggested holdover ppm floor when healthy (e.g. 2.0 for DS3231 TCXO class).
  float ppmFloorHint() const { return ppmFloorHint_; }
  float dieTempC() const { return haveTemp_ ? tempC_ : NAN; }
  const char* driver() const { return driver_; }
  const char* reason() const { return reason_; }

 private:
  bool probeDs3231();
  bool readDs3231Temp(float* outC);

  bool enabled_ = false;
  bool healthy_ = false;
  bool haveTemp_ = false;
  float tempC_ = 0.0f;
  float ppmFloorHint_ = NAN;
  const char* driver_ = "none";
  const char* reason_ = "disabled";
  uint32_t lastPollMs_ = 0;
  uint8_t failStreak_ = 0;
};

extern ExtClock gExtClock;
