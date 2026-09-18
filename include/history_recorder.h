#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "gps_service.h"
#include "local_clock.h"

// Packed 1 Hz diagnostic sample (v1). Lives in PSRAM on S3 only.
struct HistorySample {
  uint32_t utcEpoch = 0;
  int16_t residualMs = 0;
  int16_t freqPpmX100 = 0;
  int16_t tempCenti = INT16_MIN;
  int16_t tempRefCenti = INT16_MIN;
  uint16_t qualityMs = 0xFFFF;
  uint16_t holdoverSec = 0;
  uint32_t ppsCount = 0;
  uint8_t state = 0;
  uint8_t satellites = 0;
  int8_t rssi = 0;
  uint8_t flags = 0;
} __attribute__((packed));

static_assert(sizeof(HistorySample) == 24, "HistorySample v1 must be 24 bytes");

enum HistoryFlag : uint8_t {
  HistPpsFresh = 1u << 0,
  HistFix = 1u << 1,
  HistTempComp = 1u << 2,
  HistGap = 1u << 3,
};

struct HistorySummary {
  bool enabled = false;
  const char* reason = "";
  uint8_t version = 1;
  uint32_t capacity = 0;
  uint32_t count = 0;
  uint32_t intervalSec = 1;
  uint32_t seq = 0;
  uint32_t oldestUtc = 0;
  uint32_t newestUtc = 0;
  uint32_t psramBytes = 0;
  uint32_t stateCounts[5] = {};  // ACQ LCK DEG HLD UNS
  float freqMin = 0;
  float freqMax = 0;
  float freqMean = 0;
  uint32_t gaps = 0;
  uint32_t otaSkipped = 0;
  bool haveFreq = false;
};

// Snapshot of ring indices for a streaming export (task-net).
struct HistoryExportCursor {
  uint32_t seq = 0;
  uint32_t count = 0;
  uint32_t capacity = 0;
  uint32_t head = 0;  // next write index
  uint32_t start = 0; // oldest logical offset into the window
  uint32_t limit = 0; // number of samples to emit
};

class HistoryRecorder {
 public:
  void begin();
  bool enabled() const { return enabled_; }
  const char* reason() const { return reason_; }

  // task-time only. Skip when OTA busy (caller checks) or call skipOta().
  void push(const GpsStatus& st, int8_t rssi);
  void skipOta() { otaSkipped_++; }

  HistorySummary summary() const;
  HistoryExportCursor beginExport(uint32_t lastSec, uint32_t maxRows) const;
  bool sampleLogical(const HistoryExportCursor& cur, uint32_t i, HistorySample* out) const;
  // Copy up to n samples starting at logical index i0 under one lock (CSV batch).
  size_t copyLogical(const HistoryExportCursor& cur, uint32_t i0, HistorySample* dst,
                     size_t n) const;

  uint32_t count() const;
  uint32_t capacity() const { return capacity_; }
  uint32_t seq() const;
  uint32_t otaSkipped() const { return otaSkipped_; }
  uint32_t gaps() const;

 private:
  static int16_t clampI16(int32_t v);
  static int16_t encodeTempC(float c);
  static int16_t encodePpm(float ppm);
  bool lock(TickType_t ticks) const;
  void unlock() const;
  void applyStatsLocked(const HistorySample& s, int dir);  // +1 add / -1 remove
  void refreshFreqExtLocked();  // recompute min/max when dirty

  // Mutex (not portMUX): PSRAM R/W must not run with interrupts disabled —
  // that stalls WiFi/HTTP on the other core and makes the status page "停秒".
  mutable SemaphoreHandle_t mu_ = nullptr;
  HistorySample* buf_ = nullptr;
  bool enabled_ = false;
  const char* reason_ = "not started";
  uint32_t capacity_ = 0;
  uint32_t count_ = 0;
  uint32_t head_ = 0;
  uint32_t seq_ = 0;
  uint32_t lastPushMs_ = 0;
  uint32_t lastPpsCount_ = 0;
  bool haveLastPps_ = false;
  uint32_t otaSkipped_ = 0;
  uint32_t gaps_ = 0;

  // Incremental rolling stats (exact mean / state histogram).
  uint32_t stateCounts_[5] = {};
  int64_t freqSumCenti_ = 0;
  uint32_t freqN_ = 0;
  int16_t freqMinCenti_ = 0;
  int16_t freqMaxCenti_ = 0;
  bool haveFreqExt_ = false;
  bool freqExtDirty_ = false;
};

extern HistoryRecorder gHistory;
