#include "history_recorder.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

HistoryRecorder gHistory;

#ifndef HISTORY_INTERVAL_MS
#define HISTORY_INTERVAL_MS 1000u
#endif

void HistoryRecorder::begin() {
#if defined(BOARD_HAS_PSRAM)
  capacity_ = HISTORY_CAPACITY;
  const size_t bytes = static_cast<size_t>(capacity_) * sizeof(HistorySample);
  buf_ = static_cast<HistorySample*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf_ == nullptr) {
    enabled_ = false;
    reason_ = "alloc failed";
    capacity_ = 0;
    Serial.printf("[history] PSRAM alloc %u B failed — disabled\n",
                  static_cast<unsigned>(bytes));
    return;
  }
  memset(buf_, 0, bytes);
  enabled_ = true;
  reason_ = "ok";
  count_ = 0;
  head_ = 0;
  seq_ = 0;
  memset(stateCounts_, 0, sizeof(stateCounts_));
  freqSumCenti_ = 0;
  freqN_ = 0;
  Serial.printf("[history] enabled capacity=%u bytes=%u (SPIRAM)\n",
                static_cast<unsigned>(capacity_), static_cast<unsigned>(bytes));
#else
  enabled_ = false;
  reason_ = "no PSRAM";
  capacity_ = 0;
  buf_ = nullptr;
  Serial.println("[history] disabled (no PSRAM on this target)");
#endif
}

int16_t HistoryRecorder::clampI16(int32_t v) {
  if (v > 32767) {
    return 32767;
  }
  if (v < -32768) {
    return -32768;
  }
  return static_cast<int16_t>(v);
}

int16_t HistoryRecorder::encodeTempC(float c) {
  if (!isfinite(c)) {
    return INT16_MIN;
  }
  return clampI16(lroundf(c * 100.0f));
}

int16_t HistoryRecorder::encodePpm(float ppm) {
  if (!isfinite(ppm)) {
    return 0;
  }
  return clampI16(lroundf(ppm * 100.0f));
}

void HistoryRecorder::applyStatsLocked(const HistorySample& s, int dir) {
  uint8_t st = s.state;
  if (st > 4) {
    st = 0;
  }
  if (dir > 0) {
    stateCounts_[st]++;
    freqSumCenti_ += s.freqPpmX100;
    freqN_++;
  } else {
    if (stateCounts_[st] > 0) {
      stateCounts_[st]--;
    }
    freqSumCenti_ -= s.freqPpmX100;
    if (freqN_ > 0) {
      freqN_--;
    }
  }
}

void HistoryRecorder::push(const GpsStatus& st, int8_t rssi) {
  if (!enabled_ || buf_ == nullptr) {
    return;
  }
  const uint32_t now = millis();
  if (lastPushMs_ != 0 && (now - lastPushMs_) < HISTORY_INTERVAL_MS) {
    return;
  }

  HistorySample s{};
  s.utcEpoch = (st.timeValid || st.utcEpoch > 0) ? st.utcEpoch : 0;
  s.residualMs = clampI16(st.residualMs);
  s.freqPpmX100 = encodePpm(st.freqPpm);
  s.tempCenti = encodeTempC(st.tempC);
  s.tempRefCenti = encodeTempC(st.tempRefC);
  s.qualityMs = (st.qualityMs > 0xFFFEu) ? 0xFFFF : static_cast<uint16_t>(st.qualityMs);
  {
    const uint32_t hs = st.holdoverMs / 1000UL;
    s.holdoverSec = hs > 0xFFFFUL ? 0xFFFF : static_cast<uint16_t>(hs);
  }
  s.ppsCount = st.ppsCount;
  s.state = static_cast<uint8_t>(st.clockState);
  s.satellites = st.satellites;
  s.rssi = rssi;
  s.flags = 0;
  if (st.ppsFresh) {
    s.flags |= HistPpsFresh;
  }
  if (st.validFix) {
    s.flags |= HistFix;
  }
  if (st.tempComp) {
    s.flags |= HistTempComp;
  }
  if (haveLastPps_) {
    const uint32_t dPps = (st.ppsCount >= lastPpsCount_) ? (st.ppsCount - lastPpsCount_) : 0;
    const uint32_t dMs = (lastPushMs_ != 0) ? (now - lastPushMs_) : 0;
    if (dPps > 1 || dMs > (HISTORY_INTERVAL_MS + 500)) {
      s.flags |= HistGap;
    }
  }

  portENTER_CRITICAL(&mux_);
  if (count_ == capacity_) {
    applyStatsLocked(buf_[head_], -1);
  }
  buf_[head_] = s;
  applyStatsLocked(s, +1);
  head_ = (head_ + 1) % capacity_;
  if (count_ < capacity_) {
    count_++;
  }
  seq_++;
  if (s.flags & HistGap) {
    gaps_++;
  }
  portEXIT_CRITICAL(&mux_);

  lastPushMs_ = now;
  lastPpsCount_ = st.ppsCount;
  haveLastPps_ = true;
}

uint32_t HistoryRecorder::count() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t c = count_;
  portEXIT_CRITICAL(&mux_);
  return c;
}

uint32_t HistoryRecorder::seq() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t s = seq_;
  portEXIT_CRITICAL(&mux_);
  return s;
}

HistorySummary HistoryRecorder::summary() const {
  HistorySummary out;
  out.version = 1;
  out.intervalSec = 1;
  if (!enabled_ || buf_ == nullptr) {
    out.enabled = false;
    out.reason = reason_;
    out.otaSkipped = otaSkipped_;
    return out;
  }

  uint32_t count = 0;
  uint32_t head = 0;
  uint32_t capacity = 0;
  uint32_t seq = 0;
  uint32_t gaps = 0;
  uint32_t stateCounts[5] = {};
  int64_t freqSumCenti = 0;
  uint32_t freqN = 0;
  portENTER_CRITICAL(&mux_);
  count = count_;
  head = head_;
  capacity = capacity_;
  seq = seq_;
  gaps = gaps_;
  memcpy(stateCounts, stateCounts_, sizeof(stateCounts));
  freqSumCenti = freqSumCenti_;
  freqN = freqN_;
  portEXIT_CRITICAL(&mux_);

  out.enabled = true;
  out.reason = reason_;
  out.capacity = capacity;
  out.count = count;
  out.seq = seq;
  out.psramBytes = capacity * sizeof(HistorySample);
  out.otaSkipped = otaSkipped_;
  out.gaps = gaps;
  memcpy(out.stateCounts, stateCounts, sizeof(stateCounts));

  if (count == 0) {
    return out;
  }

  // Oldest / newest UTC: O(1) index read.
  const uint32_t oldestPhys = (count < capacity) ? 0 : head;
  const uint32_t newestPhys = (head + capacity - 1) % capacity;
  HistorySample oldest{};
  HistorySample newest{};
  portENTER_CRITICAL(&mux_);
  oldest = buf_[oldestPhys];
  newest = buf_[newestPhys];
  portEXIT_CRITICAL(&mux_);
  out.oldestUtc = oldest.utcEpoch;
  out.newestUtc = newest.utcEpoch;

  if (freqN > 0) {
    out.haveFreq = true;
    out.freqMean = static_cast<float>(freqSumCenti) / (100.0f * static_cast<float>(freqN));
  }

  // Min/max via coarse stride (≤256 probes) — avoids O(N) under the mux.
  const uint32_t probes = count < 256 ? count : 256;
  float freqMin = 0;
  float freqMax = 0;
  bool have = false;
  for (uint32_t p = 0; p < probes; ++p) {
    const uint32_t oldestOffset = (probes == 1) ? 0 : (p * (count - 1)) / (probes - 1);
    const uint32_t phys =
        (count < capacity) ? oldestOffset : ((head + oldestOffset) % capacity);
    HistorySample s;
    portENTER_CRITICAL(&mux_);
    s = buf_[phys];
    portEXIT_CRITICAL(&mux_);
    const float ppm = static_cast<float>(s.freqPpmX100) * 0.01f;
    if (!have) {
      freqMin = freqMax = ppm;
      have = true;
    } else {
      if (ppm < freqMin) {
        freqMin = ppm;
      }
      if (ppm > freqMax) {
        freqMax = ppm;
      }
    }
  }
  if (have) {
    out.haveFreq = true;
    out.freqMin = freqMin;
    out.freqMax = freqMax;
  }
  return out;
}

HistoryExportCursor HistoryRecorder::beginExport(uint32_t lastSec, uint32_t maxRows) const {
  HistoryExportCursor cur;
  if (!enabled_ || buf_ == nullptr) {
    return cur;
  }
  portENTER_CRITICAL(&mux_);
  cur.seq = seq_;
  cur.count = count_;
  cur.capacity = capacity_;
  cur.head = head_;
  portEXIT_CRITICAL(&mux_);

  uint32_t limit = cur.count;
  if (lastSec > 0 && lastSec < limit) {
    limit = lastSec;
  }
  if (maxRows > 0 && maxRows < limit) {
    limit = maxRows;
  }
  cur.start = cur.count - limit;
  cur.limit = limit;
  return cur;
}

bool HistoryRecorder::sampleLogical(const HistoryExportCursor& cur, uint32_t i,
                                    HistorySample* out) const {
  if (out == nullptr || !enabled_ || buf_ == nullptr || i >= cur.limit) {
    return false;
  }
  const uint32_t oldestOffset = cur.start + i;
  const uint32_t phys =
      (cur.count < cur.capacity) ? oldestOffset : ((cur.head + oldestOffset) % cur.capacity);
  portENTER_CRITICAL(&mux_);
  *out = buf_[phys];
  portEXIT_CRITICAL(&mux_);
  return true;
}
