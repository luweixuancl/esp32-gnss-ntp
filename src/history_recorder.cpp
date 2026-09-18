#include "history_recorder.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "app_ipc.h"

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
    // Fallback: try generic malloc (some builds map PSRAM via malloc).
    buf_ = static_cast<HistorySample*>(malloc(bytes));
  }
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

uint32_t HistoryRecorder::physIndex(uint32_t oldestOffset) const {
  if (count_ < capacity_) {
    return oldestOffset;
  }
  return (head_ + oldestOffset) % capacity_;
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
  buf_[head_] = s;
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
  portENTER_CRITICAL(&mux_);
  count = count_;
  head = head_;
  capacity = capacity_;
  seq = seq_;
  portEXIT_CRITICAL(&mux_);

  out.enabled = true;
  out.reason = reason_;
  out.capacity = capacity;
  out.count = count;
  out.seq = seq;
  out.psramBytes = capacity * sizeof(HistorySample);
  out.otaSkipped = otaSkipped_;
  out.gaps = gaps_;

  if (count == 0) {
    return out;
  }

  double freqSum = 0;
  uint32_t freqN = 0;
  float freqMin = 0;
  float freqMax = 0;
  bool haveFreq = false;
  uint32_t oldestUtc = 0;
  uint32_t newestUtc = 0;

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t phys =
        (count < capacity) ? i : ((head + i) % capacity);
    HistorySample s;
    portENTER_CRITICAL(&mux_);
    s = buf_[phys];
    portEXIT_CRITICAL(&mux_);

    if (i == 0) {
      oldestUtc = s.utcEpoch;
    }
    if (i + 1 == count) {
      newestUtc = s.utcEpoch;
    }

    uint8_t st = s.state;
    if (st > 4) {
      st = 0;
    }
    out.stateCounts[st]++;

    const float ppm = static_cast<float>(s.freqPpmX100) * 0.01f;
    if (!haveFreq) {
      freqMin = freqMax = ppm;
      haveFreq = true;
    } else {
      if (ppm < freqMin) {
        freqMin = ppm;
      }
      if (ppm > freqMax) {
        freqMax = ppm;
      }
    }
    freqSum += ppm;
    freqN++;
  }

  out.oldestUtc = oldestUtc;
  out.newestUtc = newestUtc;
  out.haveFreq = haveFreq;
  if (haveFreq && freqN > 0) {
    out.freqMin = freqMin;
    out.freqMax = freqMax;
    out.freqMean = static_cast<float>(freqSum / static_cast<double>(freqN));
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
