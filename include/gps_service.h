#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "local_clock.h"
#include "settings.h"

struct GpsStatus {
  bool validFix = false;
  uint8_t satellites = 0;
  double lat = 0;
  double lon = 0;
  float hdop = 99.9f;
  uint32_t utcEpoch = 0;
  uint32_t ageMs = 0xFFFFFFFF;
  bool ppsSeen = false;
  uint32_t ppsCount = 0;
  bool ppsFresh = false;
  uint32_t qualityMs = 0xFFFFFFFF;
  bool timeValid = false;
  ClockState clockState = ClockState::Acquiring;
  int32_t residualMs = 0;
  float freqPpm = 0;
  float tempC = NAN;
  float tempRefC = NAN;
  float tempCorrPpm = 0;
  bool tempComp = false;
  uint32_t holdoverMs = 0;
  bool extClockEnabled = false;
  bool extClockHealthy = false;
  float extClockPpmFloor = NAN;
  float extClockTempC = NAN;
  const char* extClockDriver = "none";
  // RMT RX hardware capture diagnostics (docs/s3_deep_dive_roadmap.md #1).
  struct PpsRmtStats {
    bool ok = false;        // capture armed
    bool active = false;    // RMT is currently the merge source
    uint32_t samples = 0;   // refined edges delivered
    int32_t deltaMeanUx10 = 0;  // (rmt - gpio) mean, 0.1 µs units
    int32_t deltaMinUs = 0;
    int32_t deltaMaxUs = 0;
    uint32_t oddPulse = 0;  // pulse width outside the plausible band
    uint32_t fallbacks = 0; // auto-fallbacks to the GPIO source
    uint32_t lastWidthUs = 0;
    // direct-IDF driver probe (bypasses the HAL rmtRead wrapper)
    bool idfOk = false;
    uint32_t idfFrames = 0;
    uint32_t idfFirstSyms = 0;
    uint32_t idfLastSyms = 0;
    uint32_t idfLastD0Us = 0;
    uint32_t idfLastD1Us = 0;
    uint32_t idfEmptyFrames = 0;
    uint32_t idfDataFrames = 0;
    uint32_t idfRawStatus = 0;
    uint8_t idfStage = 0;
    int idfErr = 0;
  };
  PpsRmtStats ppsRmt;
};

class GpsService {
 public:
  void begin();
  // Call only from task-time.
  void loop(AnomalyPolicy policy, uint16_t holdoverSec);
  void setTempComp(bool enabled, int16_t coeffCenti);

  GpsStatus snapshot() const;

  bool ppsFresh() const;
  bool nowUtc(uint32_t& seconds, uint32_t& fraction) const;
  bool referenceUtc(uint32_t& seconds, uint32_t& fraction) const;
  uint32_t qualityMs() const;
  const LocalClock& localClock() const { return localClock_; }

  void setTimeTask(TaskHandle_t handle) { timeTask_ = handle; }

 private:
  static void IRAM_ATTR onPpsIsr();
  static void rmtPpsCb(uint32_t* data, size_t len, void* arg);
  static bool rmtProcessSymbols(const uint32_t* data, size_t len);
  static void rmtIdfTask(void* arg);
  void parseNmea();
  void commitNmeaTime(uint32_t epochSec, AnomalyPolicy policy, uint16_t holdoverSec);
  void publishStatus(const GpsStatus& work);
  void sampleDieTemp();
  void feedNmeaChar(char c);
  void onNmeaLine(const char* line);
  bool parseZdaLine(const char* line);
  static uint8_t nmeaChecksum(const char* body);
  void sendPcas(const char* bodyNoDollar);
  void probeAndFilterNmea();
  static uint32_t civilToEpoch(int year, int month, int day, int hour, int minute, int second);

  HardwareSerial gpsSerial_{GPS_UART_NUM};
  TinyGPSPlus gps_;
  GpsStatus published_;
  LocalClock localClock_;
  uint32_t lastDebugMs_ = 0;

  uint32_t commitEpoch_ = 0;
  uint32_t commitPpsCount_ = 0;
  uint32_t commitMs_ = 0;
  uint32_t lastCommittedSecond_ = 0xFFFFFFFF;
  bool haveCommit_ = false;
  bool ppsSeen_ = false;
  uint32_t lastDrainedPpsCount_ = 0;
  bool tempSensorOk_ = false;
  uint32_t lastTempMs_ = 0;
  uint32_t lastTempTryMs_ = 0;

  // Line assembly for ZDA / talker probe (TinyGPSPlus has no native ZDA).
  char nmeaLine_[96] = {};
  size_t nmeaLineLen_ = 0;
  bool zdaValid_ = false;
  uint32_t zdaEpoch_ = 0;
  uint32_t zdaMs_ = 0;
  // Bitmask of seen sentence types during boot probe / runtime (for logs).
  uint16_t nmeaSeenMask_ = 0;
  bool nmeaFilterApplied_ = false;

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  struct PpsIsrEdge {
    uint64_t edgeUs;
    uint32_t count;
  };

  static portMUX_TYPE ppsMux_;
  static volatile uint32_t ppsCount_;
  static volatile uint64_t ppsLastEdgeUs_;
  static volatile uint8_t ppsQHead_;
  static volatile uint8_t ppsQTail_;
  static volatile PpsIsrEdge ppsQ_[GPS_PPS_ISR_QUEUE];
  static TaskHandle_t timeTask_;
};
