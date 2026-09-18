#include "gps_service.h"
#include <esp_timer.h>
#if defined(ARDUINO_ESP32S3_DEV)
// S3 has the newer tsens hardware; the legacy driver below does not exist
// for it. Arduino's temperatureRead() HAL picks the per-target API.
#else
#include "driver/temp_sensor.h"
#endif

portMUX_TYPE GpsService::ppsMux_ = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t GpsService::ppsCount_ = 0;
volatile uint64_t GpsService::ppsLastEdgeUs_ = 0;
volatile uint8_t GpsService::ppsQHead_ = 0;
volatile uint8_t GpsService::ppsQTail_ = 0;
volatile GpsService::PpsIsrEdge GpsService::ppsQ_[GPS_PPS_ISR_QUEUE] = {};
TaskHandle_t GpsService::timeTask_ = nullptr;

// --- RMT RX hardware capture (docs/s3_deep_dive_roadmap.md #1) -----------
// The RMT channel samples the pad at GPS_PPS_RMT_TICK_NS resolution; each
// frame (edge + capture window) is delivered to rmtPpsCb with the exact
// hardware-measured symbol lengths, so the edge time is reconstructed as
// cb_entry - frame_length — immune to GPIO-ISR latency (incl. WiFi storms).
// GPIO ISR stays attached; loop() merges by edge count and records deltas.
#if GPS_PPS_RMT_EN
struct RmtPpsEdge {
  uint64_t edgeUs;
  uint32_t count;
  uint32_t widthUs;
};
struct RmtPpsState {
  bool armed = false;   // rmtInit + rmtRead succeeded
  bool alive = false;   // refinements still flowing
  bool active = false;  // currently the merge source
  bool reportedFallback = false;  // one log line per fallback episode
  uint64_t lastEdgeUs = 0;
  uint8_t head = 0, tail = 0;
  RmtPpsEdge q[GPS_PPS_RMT_QUEUE];
  // statistics (task-context writers only)
  uint32_t samples = 0;
  int64_t deltaSumUx10 = 0;
  int32_t deltaMinUs = 0;
  int32_t deltaMaxUs = 0;
  uint32_t oddPulse = 0;
  uint32_t fallbacks = 0;
  uint32_t lastWidthUs = 0;
};
static RmtPpsState gRmt;
#endif

#if defined(ARDUINO_ESP32S3_DEV)
static bool boardTempBegin() {
  // temperatureRead() lazily initialises the new tsens driver; gate on a
  // plausible first reading instead of a driver return code.
  float c = temperatureRead();
  const bool ok = !isnan(c) && c > -20.0f && c < 85.0f;
  return ok;
}
static bool boardTempRead(float* out) {
  float c = temperatureRead();
  if (isnan(c) || c <= -20.0f || c >= 85.0f) {
    return false;
  }
  *out = c;
  return true;
}
#else
static bool boardTempBegin() {
  temp_sensor_config_t tsens = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(tsens);
  const esp_err_t err = temp_sensor_start();
  return (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}
static bool boardTempRead(float* out) {
  return temp_sensor_read_celsius(out) == ESP_OK;
}
#endif

void IRAM_ATTR GpsService::onPpsIsr() {
  const uint64_t edgeUs = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&ppsMux_);
  const uint32_t count = ++ppsCount_;
  ppsLastEdgeUs_ = edgeUs;

  const uint8_t next = static_cast<uint8_t>((ppsQHead_ + 1) % GPS_PPS_ISR_QUEUE);
  if (next == ppsQTail_) {
    // Drop oldest so newest edges survive under load.
    ppsQTail_ = static_cast<uint8_t>((ppsQTail_ + 1) % GPS_PPS_ISR_QUEUE);
  }
  ppsQ_[ppsQHead_].edgeUs = edgeUs;
  ppsQ_[ppsQHead_].count = count;
  ppsQHead_ = next;
  portEXIT_CRITICAL_ISR(&ppsMux_);

  BaseType_t woken = pdFALSE;
  if (timeTask_ != nullptr) {
    vTaskNotifyGiveFromISR(timeTask_, &woken);
  }
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

// HAL delivers frames from a normal TASK context (_rmtRxTask), not an ISR:
// plain critical-section/notify APIs are the correct ones here. A frame is
// ~one second of symbols ([low gap][pulse][trailing low]) split at the idle
// threshold, so the edge must be located by the LAST 0->1 transition in the
// frame — reconstructing from the total frame length would land ~one second
// early (the ACQ-oscillation bug of the first attempt).
void GpsService::rmtPpsCb(uint32_t* data, size_t len, void* arg) {
  (void)arg;
#if GPS_PPS_RMT_EN
  if (data == nullptr || len == 0) {
    return;
  }
  const uint64_t nowUs = esp_timer_get_time();

  // Walk the symbol halves in order; remember where the last rising edge
  // starts and the length of the high run after it (the pulse may span
  // several symbols: one symbol caps at 32767 ticks = ~32.8 ms @1 µs).
  uint64_t cumNs = 0;
  uint64_t riseNs = 0;
  uint64_t widthNs = 0;
  bool haveEdge = false;
  int prevLevel = 0;
  for (size_t i = 0; i < len; ++i) {
    const uint32_t sym = data[i];
    const uint32_t durs[2] = {sym & 0x7FFF, (sym >> 16) & 0x7FFF};
    const int levels[2] = {static_cast<int>((sym >> 15) & 1),
                           static_cast<int>((sym >> 31) & 1)};
    for (int h = 0; h < 2; ++h) {
      if (durs[h] == 0) {
        continue;
      }
      const uint64_t dNs = static_cast<uint64_t>(durs[h]) * GPS_PPS_RMT_TICK_NS;
      if (prevLevel == 0 && levels[h] == 1) {
        riseNs = cumNs;
        widthNs = 0;
        haveEdge = true;
      }
      if (levels[h] == 1 && haveEdge) {
        widthNs += dNs;
      }
      cumNs += dNs;
      prevLevel = levels[h];
    }
  }
  if (!haveEdge) {
    return;
  }
  const uint32_t widthUs = static_cast<uint32_t>(widthNs / 1000);
  const uint64_t edgeUs = nowUs - ((cumNs - riseNs) / 1000);
  // Sanity gates: a valid PPS edge happened recently and its pulse is
  // plausible; anything else is a malformed frame — drop, never poison.
  if (widthUs < 10000 || widthUs > 500000) {
    portENTER_CRITICAL(&ppsMux_);
    ++gRmt.oddPulse;
    portEXIT_CRITICAL(&ppsMux_);
    return;
  }
  if (nowUs - edgeUs > 1500000ULL) {
    return;  // reconstructed edge older than 1.5 s: malformed frame
  }

  portENTER_CRITICAL(&ppsMux_);
  const uint32_t count = ppsCount_;  // GPIO ISR owns edge counting
  const uint8_t next = static_cast<uint8_t>((gRmt.head + 1) % GPS_PPS_RMT_QUEUE);
  if (next != gRmt.tail) {
    gRmt.q[gRmt.head].edgeUs = edgeUs;
    gRmt.q[gRmt.head].count = count;
    gRmt.q[gRmt.head].widthUs = widthUs;
    gRmt.head = next;
  }
  gRmt.lastEdgeUs = nowUs;
  gRmt.alive = true;
  gRmt.lastWidthUs = widthUs;
  portEXIT_CRITICAL(&ppsMux_);

  if (timeTask_ != nullptr) {
    xTaskNotifyGive(timeTask_);
  }
#endif
}

void GpsService::begin() {
  localClock_.reset();
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  gpsSerial_.setRxBufferSize(2048);
  gpsSerial_.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsIsr, RISING);
#if GPS_PPS_RMT_EN
  {
    rmt_obj_t* r = rmtInit(PIN_GPS_PPS, RMT_RX_MODE, RMT_MEM_64);
    if (r == nullptr) {
      Serial.println("[pps-rmt] init failed -> GPIO ISR only");
    } else {
      rmtSetTick(r, GPS_PPS_RMT_TICK_NS);
      rmtSetFilter(r, true, GPS_PPS_RMT_FILTER_NS / GPS_PPS_RMT_TICK_NS);
      rmtSetRxThreshold(r, static_cast<uint32_t>(GPS_PPS_RMT_WINDOW_MS * 1000000UL) / GPS_PPS_RMT_TICK_NS);
      if (rmtRead(r, &GpsService::rmtPpsCb, nullptr)) {
        gRmt.armed = true;
        Serial.printf("[pps-rmt] armed pin=%d tick=%uns win=%ums filter=%uns\n",
                      PIN_GPS_PPS, GPS_PPS_RMT_TICK_NS, GPS_PPS_RMT_WINDOW_MS,
                      GPS_PPS_RMT_FILTER_NS);
      } else {
        Serial.println("[pps-rmt] rmtRead failed -> GPIO ISR only");
      }
    }
  }
#endif
  tempSensorOk_ = boardTempBegin();
  Serial.printf("GPS UART%d RX=%d TX=%d baud=%d buf=2048 local-clock=on ppsQ=%d tsens=%d\n",
                GPS_UART_NUM, PIN_GPS_RX, PIN_GPS_TX, GPS_UART_BAUD, GPS_PPS_ISR_QUEUE,
                tempSensorOk_ ? 1 : 0);
}

void GpsService::setTempComp(bool enabled, int16_t coeffCenti) {
  localClock_.setTempComp(enabled, coeffCenti);
}


void GpsService::sampleDieTemp() {
  if (!tempSensorOk_) {
    return;
  }
  const uint32_t now = millis();
  if (lastTempMs_ != 0 && (now - lastTempMs_) < CLK_TEMP_SAMPLE_MS) {
    return;
  }
  lastTempMs_ = now;
  float c = NAN;
  if (boardTempRead(&c)) {
    localClock_.updateDieTemp(c);
  }
}

void GpsService::loop(AnomalyPolicy policy, uint16_t holdoverSec) {
  sampleDieTemp();
  parseNmea();

  // Drain every queued PPS edge (WiFi may delay task-time by >1s). With RMT
  // capture armed, GPIO edges are held briefly until their refined hardware
  // timestamp arrives (keyed by count); per-edge timeout falls back to the
  // GPIO timestamp. Delta statistics quantify the RMT gain (roadmap #1).
  uint32_t drainedCount = 0;
#if GPS_PPS_RMT_EN
  struct GpioPending {
    uint64_t edgeUs;
    uint32_t count;
    uint64_t queuedUs;
  };
  static GpioPending pend[GPS_PPS_ISR_QUEUE];
  static size_t npend = 0;
  RmtPpsEdge ref[GPS_PPS_RMT_QUEUE];
  size_t nref = 0;
  if (gRmt.armed) {
    portENTER_CRITICAL(&ppsMux_);
    while (gRmt.tail != gRmt.head && nref < GPS_PPS_RMT_QUEUE) {
      ref[nref++] = gRmt.q[gRmt.tail];
      gRmt.tail = static_cast<uint8_t>((gRmt.tail + 1) % GPS_PPS_RMT_QUEUE);
    }
    portEXIT_CRITICAL(&ppsMux_);
  }
#endif
  for (;;) {
    uint64_t edgeUs = 0;
    uint32_t count = 0;
    bool got = false;
    portENTER_CRITICAL(&ppsMux_);
    if (ppsQTail_ != ppsQHead_) {
      edgeUs = ppsQ_[ppsQTail_].edgeUs;
      count = ppsQ_[ppsQTail_].count;
      ppsQTail_ = static_cast<uint8_t>((ppsQTail_ + 1) % GPS_PPS_ISR_QUEUE);
      got = true;
    }
    portEXIT_CRITICAL(&ppsMux_);
    if (!got) {
      break;
    }
    ppsSeen_ = true;
    drainedCount = count;
#if GPS_PPS_RMT_EN
    if (gRmt.armed) {
      if (npend < GPS_PPS_ISR_QUEUE) {
        pend[npend].edgeUs = edgeUs;
        pend[npend].count = count;
        pend[npend].queuedUs = esp_timer_get_time();
        ++npend;
      } else {
        localClock_.onPpsEdge(edgeUs, count);  // pending full: emit raw
      }
      continue;
    }
#endif
    localClock_.onPpsEdge(edgeUs, count);
  }
#if GPS_PPS_RMT_EN
  if (gRmt.armed && npend != 0) {
    const uint64_t nowUs = esp_timer_get_time();
    const uint64_t holdNs = static_cast<uint64_t>(GPS_PPS_RMT_WINDOW_MS + 5) * 1000000ULL;
    size_t out = 0;
    for (size_t i = 0; i < npend; ++i) {
      int64_t refined = -1;
      for (size_t k = 0; k < nref; ++k) {
        if (ref[k].count == pend[i].count) {
          refined = static_cast<int64_t>(ref[k].edgeUs);
          break;
        }
      }
      if (refined >= 0) {
        localClock_.onPpsEdge(static_cast<uint64_t>(refined), pend[i].count);
        const int64_t dUs = refined - static_cast<int64_t>(pend[i].edgeUs);
        ++gRmt.samples;
        gRmt.deltaSumUx10 += dUs * 10;
        if (gRmt.samples == 1 || dUs < gRmt.deltaMinUs) gRmt.deltaMinUs = static_cast<int32_t>(dUs);
        if (gRmt.samples == 1 || dUs > gRmt.deltaMaxUs) gRmt.deltaMaxUs = static_cast<int32_t>(dUs);
        if (!gRmt.active) {
          gRmt.active = true;
          gRmt.reportedFallback = false;
        }
        drainedCount = pend[i].count;
      } else if ((nowUs - pend[i].queuedUs) > holdNs) {
        localClock_.onPpsEdge(pend[i].edgeUs, pend[i].count);  // no refinement: GPIO fallback
        if (gRmt.active) {
          gRmt.active = false;
          ++gRmt.fallbacks;
          if (!gRmt.reportedFallback) {
            Serial.println("[pps-rmt] stale -> GPIO fallback");
            gRmt.reportedFallback = true;
          }
        }
        drainedCount = pend[i].count;
      } else {
        pend[out++] = pend[i];  // still within the capture window: keep waiting
      }
    }
    npend = out;
    // Refinements whose GPIO twin was dropped (ISR storm): feed them anyway.
    for (size_t k = 0; k < nref; ++k) {
      bool matched = false;
      for (size_t i = 0; i < npend; ++i) {
        if (pend[i].count == ref[k].count) {
          matched = true;
          break;
        }
      }
      if (!matched && ref[k].count > drainedCount) {
        localClock_.onPpsEdge(ref[k].edgeUs, ref[k].count);
        drainedCount = ref[k].count;
        ++gRmt.samples;
      }
    }
  }
#endif
  if (drainedCount != 0) {
    lastDrainedPpsCount_ = drainedCount;
  }

  GpsStatus work;
  work.satellites = gps_.satellites.isValid() ? gps_.satellites.value() : 0;
  // isValid() alone is sticky after first fix; require recent updates and sats>0 so
  // antenna-loss (0 sats / stale NMEA) clears "GPS 锁定" on OLED/Web.
  const bool locFresh =
      gps_.location.isValid() && gps_.location.age() <= GPS_FIX_MAX_AGE_MS;
  const bool timeFresh = gps_.date.isValid() && gps_.time.isValid() &&
                         gps_.time.age() <= GPS_FIX_MAX_AGE_MS;
  const bool satsOk = gps_.satellites.isValid() &&
                      gps_.satellites.age() <= GPS_FIX_MAX_AGE_MS && work.satellites > 0;
  work.validFix = locFresh && timeFresh && satsOk;
  if (locFresh) {
    work.lat = gps_.location.lat();
    work.lon = gps_.location.lng();
  }
  if (gps_.hdop.isValid() && gps_.hdop.age() <= GPS_FIX_MAX_AGE_MS) {
    work.hdop = gps_.hdop.hdop();
  }
  work.ppsSeen = ppsSeen_;
  work.ppsCount = ppsCount_;
  work.ppsFresh = ppsFresh();
#if GPS_PPS_RMT_EN
  work.ppsRmt.ok = gRmt.armed;
  work.ppsRmt.active = gRmt.active;
  work.ppsRmt.samples = gRmt.samples;
  work.ppsRmt.deltaMeanUx10 = gRmt.samples != 0
      ? static_cast<int32_t>(gRmt.deltaSumUx10 / gRmt.samples) : 0;
  work.ppsRmt.deltaMinUs = gRmt.deltaMinUs;
  work.ppsRmt.deltaMaxUs = gRmt.deltaMaxUs;
  work.ppsRmt.oddPulse = gRmt.oddPulse;
  work.ppsRmt.fallbacks = gRmt.fallbacks;
  work.ppsRmt.lastWidthUs = gRmt.lastWidthUs;
#endif

  if (gps_.date.isValid() && gps_.time.isValid()) {
    TinyGPSDate d = gps_.date;
    TinyGPSTime t = gps_.time;
    int y = d.year();
    int m = d.month();
    int day = d.day();
    if (m <= 2) {
      y -= 1;
      m += 12;
    }
    int64_t a = y / 100;
    int64_t b = 2 - a + a / 4;
    int64_t jd = static_cast<int64_t>(365.25 * (y + 4716)) +
                 static_cast<int64_t>(30.6001 * (m + 1)) + day + b - 1524;
    int64_t daysSinceUnix = jd - 2440588;
    const uint32_t epoch = static_cast<uint32_t>(daysSinceUnix * 86400LL + t.hour() * 3600L +
                                                 t.minute() * 60L + t.second());
    work.ageMs = gps_.time.age();
    if (epoch != lastCommittedSecond_) {
      commitNmeaTime(epoch, policy, holdoverSec);
      lastCommittedSecond_ = epoch;
    }
  } else {
    work.ageMs = 0xFFFFFFFF;
  }

  const bool nmeaFresh = haveCommit_ && (millis() - commitMs_) <= 3000;
  localClock_.tick(nmeaFresh, work.ppsFresh, policy, holdoverSec);

  work.qualityMs = qualityMs();
  uint32_t sec = 0;
  uint32_t frac = 0;
  work.timeValid = nowUtc(sec, frac);
  work.utcEpoch = work.timeValid ? sec : commitEpoch_;
  work.clockState = localClock_.state();
  work.residualMs = localClock_.residualMs();
  work.freqPpm = localClock_.freqPpm();
  work.tempC = localClock_.dieTempC();
  work.tempRefC = localClock_.tempRefC();
  work.tempCorrPpm = localClock_.tempCorrPpm();
  work.tempComp = localClock_.tempCompEnabled();
  work.holdoverMs = localClock_.holdoverElapsedMs();

  publishStatus(work);

#if GPS_DEBUG
  const uint32_t now = millis();
  if (now - lastDebugMs_ >= 1000) {
    lastDebugMs_ = now;
    Serial.printf(
        "GPS fix=%d sat=%u pps=%u utc=%lu valid=%d clk=%s r=%ld ppm=%.1f q=%lu hold=%lu\n",
        work.validFix ? 1 : 0, work.satellites, work.ppsCount,
        static_cast<unsigned long>(work.utcEpoch), work.timeValid ? 1 : 0,
        clockStateLabel(work.clockState), static_cast<long>(work.residualMs),
        static_cast<double>(work.freqPpm), static_cast<unsigned long>(work.qualityMs),
        static_cast<unsigned long>(work.holdoverMs));
  }
#endif
}

void GpsService::commitNmeaTime(uint32_t epochSec, AnomalyPolicy policy, uint16_t holdoverSec) {
  commitEpoch_ = epochSec;
  commitPpsCount_ = lastDrainedPpsCount_ != 0 ? lastDrainedPpsCount_ : ppsCount_;
  commitMs_ = millis();
  haveCommit_ = true;
  localClock_.onNmeaCommit(epochSec, commitPpsCount_, policy, holdoverSec);
}

void GpsService::publishStatus(const GpsStatus& work) {
  portENTER_CRITICAL(&mux_);
  published_ = work;
  portEXIT_CRITICAL(&mux_);
}

void GpsService::parseNmea() {
  int budget = GPS_NMEA_MAX_BYTES_PER_LOOP;
  while (budget-- > 0 && gpsSerial_.available() > 0) {
    const char c = static_cast<char>(gpsSerial_.read());
#if GPS_DEBUG_NMEA
    Serial.write(c);
#endif
    gps_.encode(c);
  }
}

GpsStatus GpsService::snapshot() const {
  GpsStatus out;
  portENTER_CRITICAL(&mux_);
  out = published_;
  portEXIT_CRITICAL(&mux_);
  return out;
}

bool GpsService::ppsFresh() const {
  uint64_t edge = 0;
  portENTER_CRITICAL(&ppsMux_);
  edge = ppsLastEdgeUs_;
  portEXIT_CRITICAL(&ppsMux_);
  if (edge == 0) {
    return false;
  }
  const uint64_t now = esp_timer_get_time();
  return (now >= edge) && ((now - edge) < 1500000ULL);
}

uint32_t GpsService::qualityMs() const {
  return localClock_.qualityMs();
}

bool GpsService::nowUtc(uint32_t& seconds, uint32_t& fraction) const {
  // NTP honesty: only LocalClock Locked/Degraded/Holdover may serve time.
  return localClock_.nowUtc(seconds, fraction);
}

bool GpsService::referenceUtc(uint32_t& seconds, uint32_t& fraction) const {
  return localClock_.referenceUtc(seconds, fraction);
}
