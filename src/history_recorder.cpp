#include "history_recorder.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

HistoryRecorder gHistory;

#ifndef HISTORY_INTERVAL_MS
#define HISTORY_INTERVAL_MS 1000u
#endif

bool HistoryRecorder::lock(TickType_t ticks) const {
  if (mu_ == nullptr) {
    return false;
  }
  return xSemaphoreTake(mu_, ticks) == pdTRUE;
}

void HistoryRecorder::unlock() const {
  if (mu_ != nullptr) {
    xSemaphoreGive(mu_);
  }
}

void HistoryRecorder::begin() {
#if defined(BOARD_HAS_PSRAM)
  mu_ = xSemaphoreCreateMutex();
  if (mu_ == nullptr) {
    enabled_ = false;
    reason_ = "mutex failed";
    Serial.println("[history] mutex create failed — disabled");
    return;
  }
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
  haveFreqExt_ = false;
  freqExtDirty_ = false;
  Serial.printf("[history] enabled capacity=%u bytes=%u (SPIRAM, mutex)\n",
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
    if (!haveFreqExt_) {
      freqMinCenti_ = freqMaxCenti_ = s.freqPpmX100;
      haveFreqExt_ = true;
    } else {
      if (s.freqPpmX100 < freqMinCenti_) {
        freqMinCenti_ = s.freqPpmX100;
      }
      if (s.freqPpmX100 > freqMaxCenti_) {
        freqMaxCenti_ = s.freqPpmX100;
      }
    }
  } else {
    if (stateCounts_[st] > 0) {
      stateCounts_[st]--;
    }
    freqSumCenti_ -= s.freqPpmX100;
    if (freqN_ > 0) {
      freqN_--;
    }
    if (haveFreqExt_ &&
        (s.freqPpmX100 == freqMinCenti_ || s.freqPpmX100 == freqMaxCenti_)) {
      freqExtDirty_ = true;
    }
    if (freqN_ == 0) {
      haveFreqExt_ = false;
      freqExtDirty_ = false;
    }
  }
}

void HistoryRecorder::refreshFreqExtLocked() {
  if (!freqExtDirty_ || count_ == 0 || buf_ == nullptr) {
    return;
  }
  // Coarse pass (≤256) — good enough for summary extrema, keeps export snappy.
  const uint32_t probes = count_ < 256 ? count_ : 256;
  bool have = false;
  int16_t mn = 0;
  int16_t mx = 0;
  for (uint32_t p = 0; p < probes; ++p) {
    const uint32_t oldestOffset = (probes == 1) ? 0 : (p * (count_ - 1)) / (probes - 1);
    const uint32_t phys =
        (count_ < capacity_) ? oldestOffset : ((head_ + oldestOffset) % capacity_);
    const int16_t v = buf_[phys].freqPpmX100;
    if (!have) {
      mn = mx = v;
      have = true;
    } else {
      if (v < mn) {
        mn = v;
      }
      if (v > mx) {
        mx = v;
      }
    }
  }
  if (have) {
    freqMinCenti_ = mn;
    freqMaxCenti_ = mx;
    haveFreqExt_ = true;
  }
  freqExtDirty_ = false;
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

  // Non-blocking: if export holds the mutex, skip this second rather than stall
  // task-time (NTP/PPS path).
  if (!lock(0)) {
    return;
  }
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
  if (freqExtDirty_) {
    refreshFreqExtLocked();
  }
  unlock();

  lastPushMs_ = now;
  lastPpsCount_ = st.ppsCount;
  haveLastPps_ = true;
}

uint32_t HistoryRecorder::count() const {
  if (!lock(pdMS_TO_TICKS(20))) {
    return 0;
  }
  const uint32_t c = count_;
  unlock();
  return c;
}

uint32_t HistoryRecorder::seq() const {
  if (!lock(pdMS_TO_TICKS(20))) {
    return 0;
  }
  const uint32_t s = seq_;
  unlock();
  return s;
}

uint32_t HistoryRecorder::gaps() const {
  if (!lock(pdMS_TO_TICKS(20))) {
    return 0;
  }
  const uint32_t g = gaps_;
  unlock();
  return g;
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

  if (!lock(pdMS_TO_TICKS(100))) {
    out.enabled = true;
    out.reason = "busy";
    out.otaSkipped = otaSkipped_;
    return out;
  }

  if (freqExtDirty_) {
    // const_cast: dirty refresh mutates cache only
    const_cast<HistoryRecorder*>(this)->refreshFreqExtLocked();
  }

  out.enabled = true;
  out.reason = reason_;
  out.capacity = capacity_;
  out.count = count_;
  out.seq = seq_;
  out.psramBytes = capacity_ * sizeof(HistorySample);
  out.otaSkipped = otaSkipped_;
  out.gaps = gaps_;
  memcpy(out.stateCounts, stateCounts_, sizeof(stateCounts_));

  if (count_ == 0) {
    unlock();
    return out;
  }

  const uint32_t oldestPhys = (count_ < capacity_) ? 0 : head_;
  const uint32_t newestPhys = (head_ + capacity_ - 1) % capacity_;
  out.oldestUtc = buf_[oldestPhys].utcEpoch;
  out.newestUtc = buf_[newestPhys].utcEpoch;

  if (freqN_ > 0) {
    out.haveFreq = true;
    out.freqMean = static_cast<float>(freqSumCenti_) / (100.0f * static_cast<float>(freqN_));
  }
  if (haveFreqExt_) {
    out.haveFreq = true;
    out.freqMin = static_cast<float>(freqMinCenti_) * 0.01f;
    out.freqMax = static_cast<float>(freqMaxCenti_) * 0.01f;
  }
  unlock();
  return out;
}

HistoryExportCursor HistoryRecorder::beginExport(uint32_t lastSec, uint32_t maxRows) const {
  HistoryExportCursor cur;
  if (!enabled_ || buf_ == nullptr) {
    return cur;
  }
  if (!lock(pdMS_TO_TICKS(100))) {
    return cur;
  }
  cur.seq = seq_;
  cur.count = count_;
  cur.capacity = capacity_;
  cur.head = head_;
  unlock();

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
  return copyLogical(cur, i, out, 1) == 1;
}

size_t HistoryRecorder::copyLogical(const HistoryExportCursor& cur, uint32_t i0,
                                    HistorySample* dst, size_t n) const {
  if (dst == nullptr || n == 0 || !enabled_ || buf_ == nullptr || i0 >= cur.limit) {
    return 0;
  }
  if (n > cur.limit - i0) {
    n = cur.limit - i0;
  }
  if (!lock(pdMS_TO_TICKS(100))) {
    return 0;
  }
  for (size_t k = 0; k < n; ++k) {
    const uint32_t logical = cur.start + i0 + static_cast<uint32_t>(k);
    const uint32_t phys =
        (cur.count < cur.capacity) ? logical : ((cur.head + logical) % cur.capacity);
    dst[k] = buf_[phys];
  }
  unlock();
  return n;
}
