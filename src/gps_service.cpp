#include "gps_service.h"
#include "ext_clock.h"
#include "debug_log.h"
#include "clock_trace.h"
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>
// Arduino-ESP32 3.x / IDF 5: temperatureRead() covers C3 + S3 (legacy
// driver/temp_sensor.h removed). RMT RX uses the new driver/rmt_rx.h API.
#if GPS_PPS_RMT_EN
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "esp_private/rmt.h"
#include <soc/soc_caps.h>
#include <freertos/queue.h>
#if defined(ARDUINO_ESP32S3_DEV)
#include "driver/rtc_io.h"
#endif
#endif

portMUX_TYPE GpsService::ppsMux_ = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t GpsService::ppsCount_ = 0;
volatile uint64_t GpsService::ppsLastEdgeUs_ = 0;
volatile uint8_t GpsService::ppsQHead_ = 0;
volatile uint8_t GpsService::ppsQTail_ = 0;
volatile GpsService::PpsIsrEdge GpsService::ppsQ_[GPS_PPS_ISR_QUEUE] = {};
TaskHandle_t GpsService::timeTask_ = nullptr;

// --- RMT RX hardware capture (docs/s3_deep_dive_roadmap.md #1) -----------
// IDF5 rmt_rx samples the pad at GPS_PPS_RMT_TICK_NS resolution. Each
// receive-done delivers hardware-measured symbols so the edge time is
// reconstructed as cb_entry - trailing_length — immune to GPIO-ISR latency.
// GPIO ISR stays attached; loop() merges by edge count and records deltas.
#if GPS_PPS_RMT_EN
struct RmtPpsEdge {
  uint64_t edgeUs;
  uint32_t count;
  uint32_t widthUs;
};
struct RmtPpsState {
  bool armed = false;   // first rmt_receive ok (deferred until GPIO PPS)
  bool ready = false;   // channel+task enabled; waiting for first PPS to arm
  bool alive = false;   // refinements still flowing
  bool active = false;  // currently the merge source
  bool reportedFallback = false;  // one log line per fallback episode
  uint64_t lastEdgeUs = 0;
  uint32_t lastArmTryMs = 0;
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

// IDF5 driver state: channel handle + done-queue of *copied* frames.
// v1.1.29 board fail mode: queueing edata pointers under DMA left the task
// reading 1×zero-duration symbols (stale buffer). Always copy in the ISR.
// v1.1.33: native mem block (48 words/ch on S3); dual RX buffers; raw hex dump.
#ifndef GPS_PPS_RMT_SYM_CAP
#define GPS_PPS_RMT_SYM_CAP SOC_RMT_MEM_WORDS_PER_CHANNEL
#endif
struct RmtDoneFrame {
  size_t n = 0;
  rmt_symbol_word_t syms[GPS_PPS_RMT_SYM_CAP];
};
struct IdfPpsState {
  bool ok = false;
  uint8_t stage = 0;  // 1=new_ch 2=cbs 4=enable 8=task 16=recv
  int err = 0;
  uint32_t frames = 0;
  uint32_t firstSyms = 0;
  uint32_t lastSyms = 0;
  uint32_t lastD0Us = 0;
  uint32_t lastD1Us = 0;
  uint32_t lastRawVal = 0;
  uint32_t emptyFrames = 0;
  uint32_t dataFrames = 0;
  uint32_t junkFrames = 0;
  uint32_t dumpLeft = GPS_PPS_RMT_DUMP_FRAMES;
  uint8_t rawIdx = 0;
  rmt_channel_handle_t chan = nullptr;
  QueueHandle_t doneQ = nullptr;
  rmt_symbol_word_t raw[2][GPS_PPS_RMT_SYM_CAP];
  rmt_receive_config_t recvCfg = {};
};
static IdfPpsState gIdf;
#endif

static bool boardTempBegin() {
  // temperatureRead() lazily initialises the tsens driver; gate on a
  // plausible reading. sampleDieTemp() retries this probe every 5 s when it
  // fails, so a transiently bad first read no longer disables temperature.
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

void IRAM_ATTR GpsService::onPpsIsr() {
  const uint64_t edgeUs = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&ppsMux_);
  const uint32_t count = ppsCount_ + 1;
  ppsCount_ = count;
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
bool GpsService::rmtProcessSymbols(const uint32_t* data, size_t len) {
#if GPS_PPS_RMT_EN
  if (data == nullptr || len == 0) {
    return false;
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
    return false;
  }
  const uint32_t widthUs = static_cast<uint32_t>(widthNs / 1000);
  const uint64_t edgeUs = nowUs - ((cumNs - riseNs) / 1000);
  // Sanity gates: a valid PPS edge happened recently and its pulse is
  // plausible; anything else is a malformed frame — drop, never poison.
  if (widthUs < 10000 || widthUs > 500000) {
    portENTER_CRITICAL(&ppsMux_);
    ++gRmt.oddPulse;
    portEXIT_CRITICAL(&ppsMux_);
    return false;
  }
  if (nowUs - edgeUs > 1500000ULL) {
    return false;  // reconstructed edge older than 1.5 s: malformed frame
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
  return true;
#else
  (void)data;
  (void)len;
  return false;
#endif
}

#if GPS_PPS_RMT_EN
// IDF5 on_recv_done runs in ISR: copy symbols out of the driver buffer before
// returning (DMA/ping-pong may recycle the memory immediately after).
bool IRAM_ATTR GpsService::rmtRxDoneCb(rmt_channel_handle_t channel,
                                       const rmt_rx_done_event_data_t* edata,
                                       void* user_data) {
  (void)channel;
  (void)user_data;
  if (edata == nullptr || gIdf.doneQ == nullptr) {
    return false;
  }
  RmtDoneFrame fr{};
  size_t n = edata->num_symbols;
  if (n > GPS_PPS_RMT_SYM_CAP) {
    n = GPS_PPS_RMT_SYM_CAP;
  }
  fr.n = n;
  // Word-by-word copy (avoid memcpy in ISR / cache-safe builds).
  if (n > 0 && edata->received_symbols != nullptr) {
    for (size_t i = 0; i < n; ++i) {
      fr.syms[i].val = edata->received_symbols[i].val;
    }
  }
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(gIdf.doneQ, &fr, &woken) != pdTRUE) {
    return false;
  }
  return woken == pdTRUE;
}

bool GpsService::rmtArmReceive() {
  if (gIdf.chan == nullptr) {
    return false;
  }
  gIdf.rawIdx = static_cast<uint8_t>(1u - gIdf.rawIdx);
  rmt_symbol_word_t* buf = gIdf.raw[gIdf.rawIdx];
  const esp_err_t err = rmt_receive(gIdf.chan, buf, GPS_PPS_RMT_SYM_CAP * sizeof(rmt_symbol_word_t),
                                    &gIdf.recvCfg);
  if (err != ESP_OK) {
    gIdf.err = err;
    return false;
  }
  gIdf.stage |= 16;
  return true;
}

void GpsService::tryArmRmtAfterFirstPps() {
  if (!gRmt.ready || gRmt.armed || !gIdf.ok) {
    return;
  }
  // GNSS modules do not emit 1PPS until they have time/fix; keep GPIO-only
  // until the ISR has seen at least one edge, then start RMT receive.
  if (!ppsSeen_ && ppsCount_ == 0) {
    return;
  }
  const uint32_t nowMs = millis();
  if (gRmt.lastArmTryMs != 0 && (nowMs - gRmt.lastArmTryMs) < GPS_PPS_RMT_ARM_RETRY_MS) {
    return;
  }
  gRmt.lastArmTryMs = nowMs;
  if (rmtArmReceive()) {
    gRmt.armed = true;
    debugLogf("[pps-rmt] idf5 armed after first PPS (count=%u)\n",
                  static_cast<unsigned>(ppsCount_));
  } else {
    debugLogf("[pps-rmt] arm defer failed err=%d (retry)\n", gIdf.err);
  }
}

// Task-context RX: drain copied frames → symbol parser → re-arm receive.
void GpsService::rmtRxTask(void* arg) {
  (void)arg;
  for (;;) {
    RmtDoneFrame fr{};
    if (xQueueReceive(gIdf.doneQ, &fr, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    ++gIdf.frames;
    const size_t n = fr.n;
    if (n == 0) {
      ++gIdf.emptyFrames;
    } else {
      if (gIdf.firstSyms == 0) {
        gIdf.firstSyms = static_cast<uint32_t>(n);
      }
      gIdf.lastSyms = static_cast<uint32_t>(n);
      const rmt_symbol_word_t& s0 = fr.syms[0];
      gIdf.lastRawVal = s0.val;
      gIdf.lastD0Us = static_cast<uint32_t>(s0.duration0) * (GPS_PPS_RMT_TICK_NS / 1000);
      gIdf.lastD1Us = static_cast<uint32_t>(s0.duration1) * (GPS_PPS_RMT_TICK_NS / 1000);
      if (gIdf.dumpLeft > 0) {
        --gIdf.dumpLeft;
        // Executor-requested: raw 32-bit word(s) to distinguish all-zero vs bitfield skew.
        if (n > 1) {
          debugLogf("[pps-rmt] dump n=%u val0=0x%08lx d0=%u l0=%u d1=%u l1=%u val1=0x%08lx",
                    static_cast<unsigned>(n),
                    static_cast<unsigned long>(s0.val),
                    static_cast<unsigned>(s0.duration0),
                    static_cast<unsigned>(s0.level0),
                    static_cast<unsigned>(s0.duration1),
                    static_cast<unsigned>(s0.level1),
                    static_cast<unsigned long>(fr.syms[1].val));
        } else {
          debugLogf("[pps-rmt] dump n=%u val0=0x%08lx d0=%u l0=%u d1=%u l1=%u",
                    static_cast<unsigned>(n),
                    static_cast<unsigned long>(s0.val),
                    static_cast<unsigned>(s0.duration0),
                    static_cast<unsigned>(s0.level0),
                    static_cast<unsigned>(s0.duration1),
                    static_cast<unsigned>(s0.level1));
        }
      }
      if (rmtProcessSymbols(reinterpret_cast<const uint32_t*>(fr.syms), n)) {
        ++gIdf.dataFrames;
      } else {
        ++gIdf.junkFrames;
      }
    }
    (void)rmtArmReceive();
  }
}
#endif

void GpsService::begin() {
  localClock_.reset();
#if GPS_PPS_RMT_EN && defined(ARDUINO_ESP32S3_DEV)
  // GPIO4 is RTC-capable on S3 — leave RTC domain before digital/RMT use (v1.1.35).
  if (rtc_gpio_is_valid_gpio(static_cast<gpio_num_t>(PIN_GPS_PPS))) {
    rtc_gpio_deinit(static_cast<gpio_num_t>(PIN_GPS_PPS));
  }
#endif
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  gpsSerial_.setRxBufferSize(2048);
  gpsSerial_.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
#if GPS_PPS_RMT_EN
  {
    // Claim the pad for RMT *before* Arduino attachInterrupt so the GPIO
    // matrix RX route is established first (v1.1.33).
    // signal_range_max_ns is 15-bit in tick units → max 32767 * tick_ns.
    gIdf.doneQ = xQueueCreate(4, sizeof(RmtDoneFrame));
    gIdf.recvCfg.signal_range_min_ns = GPS_PPS_RMT_FILTER_NS;
    {
      const uint32_t tickNs = GPS_PPS_RMT_TICK_NS;
      const uint32_t maxRangeNs = 32767u * tickNs;  // IDF5 rmt_receive hard cap
      uint32_t rangeNs =
          static_cast<uint32_t>(GPS_PPS_RMT_WINDOW_MS) * 1000000UL;
      if (rangeNs > maxRangeNs) {
        rangeNs = maxRangeNs;
      }
      gIdf.recvCfg.signal_range_max_ns = rangeNs;
    }
    gIdf.recvCfg.flags.en_partial_rx = 0;

    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num = static_cast<gpio_num_t>(PIN_GPS_PPS);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = 1000000000UL / GPS_PPS_RMT_TICK_NS;  // 1 MHz @ 1 µs
#if GPS_PPS_RMT_DMA
    // DMA mode: mem_block_symbols sizes the DMA/user buffer, not HW block count.
    cfg.mem_block_symbols = GPS_PPS_RMT_SYM_CAP;
    cfg.flags.with_dma = 1;
#else
    cfg.mem_block_symbols = GPS_PPS_RMT_SYM_CAP;
    cfg.flags.with_dma = 0;
#endif
    cfg.intr_priority = 0;

    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (gIdf.doneQ == nullptr) {
      err = ESP_ERR_NO_MEM;
    } else {
      err = rmt_new_rx_channel(&cfg, &gIdf.chan);
#if GPS_PPS_RMT_DMA
      if (err != ESP_OK) {
        debugLogf("[pps-rmt] DMA channel alloc failed err=%d — fallback non-DMA",
                  static_cast<int>(err));
        cfg.flags.with_dma = 0;
        err = rmt_new_rx_channel(&cfg, &gIdf.chan);
      }
#endif
    }
    if (err == ESP_OK) {
      gIdf.stage |= 1;
      rmt_rx_event_callbacks_t cbs = {};
      cbs.on_recv_done = &GpsService::rmtRxDoneCb;
      err = rmt_rx_register_event_callbacks(gIdf.chan, &cbs, nullptr);
    }
    if (err == ESP_OK) {
      gIdf.stage |= 2;
      err = rmt_enable(gIdf.chan);
    }
    if (err == ESP_OK) {
      gIdf.stage |= 4;
      if (xTaskCreate(rmtRxTask, "rmtpps", 3072, nullptr, 3, nullptr) == pdPASS) {
        gIdf.stage |= 8;
      } else {
        err = ESP_ERR_NO_MEM;
      }
    }
    if (err == ESP_OK) {
      // Driver enables pull-up; PPS line is idle-low — restore pulldown.
      gpio_pullup_dis(static_cast<gpio_num_t>(PIN_GPS_PPS));
      gpio_pulldown_en(static_cast<gpio_num_t>(PIN_GPS_PPS));
      // Do NOT rmt_receive yet: PPS pin is idle-low until GNSS has a fix.
      // Arm on first GPIO PPS from loop() (see tryArmRmtAfterFirstPps).
      gIdf.ok = true;
      gRmt.ready = true;
      uint32_t realHz = 0;
      int chId = -1;
      (void)rmt_get_channel_resolution(gIdf.chan, &realHz);
      (void)rmt_get_channel_id(gIdf.chan, &chId);
      debugLogf("[pps-rmt] idf5 ready pin=%d tick=%uns win=%ums filter=%uns dma=%u "
                    "mem=%u ch=%d realHz=%lu (arm on first PPS)\n",
                    PIN_GPS_PPS, GPS_PPS_RMT_TICK_NS, GPS_PPS_RMT_WINDOW_MS,
                    GPS_PPS_RMT_FILTER_NS, static_cast<unsigned>(cfg.flags.with_dma),
                    static_cast<unsigned>(GPS_PPS_RMT_SYM_CAP), chId,
                    static_cast<unsigned long>(realHz));
    } else {
      gIdf.err = err;
      debugLogf("[pps-rmt] idf5 init failed stage=%u err=%d -> GPIO ISR only\n",
                    gIdf.stage, gIdf.err);
    }
  }
#endif
  // GPIO ISR after RMT matrix connect so both can share the pad.
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsIsr, RISING);
  debugLogf("GPS UART%d RX=%d TX=%d baud=%d buf=2048 local-clock=on ppsQ=%d tsens=%d\n",
                GPS_UART_NUM, PIN_GPS_RX, PIN_GPS_TX, GPS_UART_BAUD, GPS_PPS_ISR_QUEUE,
                tempSensorOk_ ? 1 : 0);
#if GPS_NMEA_FILTER_EN
  probeAndFilterNmea();
#endif
}

uint8_t GpsService::nmeaChecksum(const char* body) {
  uint8_t cs = 0;
  for (const char* p = body; p && *p; ++p) {
    cs ^= static_cast<uint8_t>(*p);
  }
  return cs;
}

void GpsService::sendPcas(const char* bodyNoDollar) {
  if (bodyNoDollar == nullptr) {
    return;
  }
  const uint8_t cs = nmeaChecksum(bodyNoDollar);
  char frame[96];
  snprintf(frame, sizeof(frame), "$%s*%02X\r\n", bodyNoDollar, cs);
  gpsSerial_.print(frame);
  debugLogf("[gps] TX %s", frame);
}

uint32_t GpsService::civilToEpoch(int year, int month, int day, int hour, int minute, int second) {
  int y = year;
  int m = month;
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  const int64_t a = y / 100;
  const int64_t b = 2 - a + a / 4;
  const int64_t jd = static_cast<int64_t>(365.25 * (y + 4716)) +
                     static_cast<int64_t>(30.6001 * (m + 1)) + day + b - 1524;
  const int64_t daysSinceUnix = jd - 2440588;
  return static_cast<uint32_t>(daysSinceUnix * 86400LL + hour * 3600L + minute * 60L + second);
}

void GpsService::feedNmeaChar(char c) {
  gps_.encode(c);
  if (c == '$') {
    nmeaLineLen_ = 0;
    nmeaLine_[nmeaLineLen_++] = c;
    return;
  }
  if (c == '\r' || c == '\n') {
    if (nmeaLineLen_ >= 6) {
      nmeaLine_[nmeaLineLen_] = '\0';
      onNmeaLine(nmeaLine_);
    }
    nmeaLineLen_ = 0;
    return;
  }
  if (nmeaLineLen_ > 0 && nmeaLineLen_ + 1 < sizeof(nmeaLine_)) {
    nmeaLine_[nmeaLineLen_++] = c;
  } else if (nmeaLineLen_ + 1 >= sizeof(nmeaLine_)) {
    nmeaLineLen_ = 0;
  }
}

void GpsService::onNmeaLine(const char* line) {
  if (line == nullptr || line[0] != '$' || strlen(line) < 6) {
    return;
  }
  // "$xxTTT,..." → sentence type at [3..5]
  const char t0 = line[3];
  const char t1 = line[4];
  const char t2 = line[5];
  if (t0 == 'G' && t1 == 'G' && t2 == 'A') {
    nmeaSeenMask_ |= 1u << 0;
  } else if (t0 == 'G' && t1 == 'L' && t2 == 'L') {
    nmeaSeenMask_ |= 1u << 1;
  } else if (t0 == 'G' && t1 == 'S' && t2 == 'A') {
    nmeaSeenMask_ |= 1u << 2;
  } else if (t0 == 'G' && t1 == 'S' && t2 == 'V') {
    nmeaSeenMask_ |= 1u << 3;
  } else if (t0 == 'R' && t1 == 'M' && t2 == 'C') {
    nmeaSeenMask_ |= 1u << 4;
  } else if (t0 == 'V' && t1 == 'T' && t2 == 'G') {
    nmeaSeenMask_ |= 1u << 5;
  } else if (t0 == 'Z' && t1 == 'D' && t2 == 'A') {
    nmeaSeenMask_ |= 1u << 6;
    parseZdaLine(line);
  }
}

bool GpsService::parseZdaLine(const char* line) {
  // $--ZDA,hhmmss.ss,dd,mm,yyyy,ltzh,ltzm*CS
  char buf[96];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char* star = strchr(buf, '*');
  if (star) {
    *star = '\0';
  }

  char* save = nullptr;
  char* tok = strtok_r(buf, ",", &save);  // $xxZDA
  if (tok == nullptr) {
    return false;
  }
  tok = strtok_r(nullptr, ",", &save);  // time
  if (tok == nullptr || strlen(tok) < 6) {
    return false;
  }
  const int hour = (tok[0] - '0') * 10 + (tok[1] - '0');
  const int minute = (tok[2] - '0') * 10 + (tok[3] - '0');
  const int second = (tok[4] - '0') * 10 + (tok[5] - '0');
  tok = strtok_r(nullptr, ",", &save);  // day
  if (tok == nullptr || *tok == '\0') {
    return false;
  }
  const int day = atoi(tok);
  tok = strtok_r(nullptr, ",", &save);  // month
  if (tok == nullptr || *tok == '\0') {
    return false;
  }
  const int month = atoi(tok);
  tok = strtok_r(nullptr, ",", &save);  // year
  if (tok == nullptr || *tok == '\0') {
    return false;
  }
  const int year = atoi(tok);
  if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60) {
    return false;
  }
  zdaEpoch_ = civilToEpoch(year, month, day, hour, minute, second);
  zdaMs_ = millis();
  zdaValid_ = true;
  return true;
}

void GpsService::probeAndFilterNmea() {
  nmeaSeenMask_ = 0;
  nmeaLineLen_ = 0;
  const uint32_t start = millis();
  debugLogf("[gps] probing NMEA ≤%u ms...\n", static_cast<unsigned>(GPS_NMEA_PROBE_MS));
  while ((millis() - start) < GPS_NMEA_PROBE_MS) {
    while (gpsSerial_.available() > 0) {
      feedNmeaChar(static_cast<char>(gpsSerial_.read()));
    }
    delay(5);
  }

  char seen[48] = {};
  size_t n = 0;
  auto append = [&](const char* s) {
    if (n > 0 && n + 1 < sizeof(seen)) {
      seen[n++] = ',';
    }
    while (*s && n + 1 < sizeof(seen)) {
      seen[n++] = *s++;
    }
    seen[n] = '\0';
  };
  if (nmeaSeenMask_ & (1u << 0)) {
    append("GGA");
  }
  if (nmeaSeenMask_ & (1u << 1)) {
    append("GLL");
  }
  if (nmeaSeenMask_ & (1u << 2)) {
    append("GSA");
  }
  if (nmeaSeenMask_ & (1u << 3)) {
    append("GSV");
  }
  if (nmeaSeenMask_ & (1u << 4)) {
    append("RMC");
  }
  if (nmeaSeenMask_ & (1u << 5)) {
    append("VTG");
  }
  if (nmeaSeenMask_ & (1u << 6)) {
    append("ZDA");
  }
  if (n == 0) {
    debugLogf("[gps] probe: no NMEA yet (module waking?) — apply RAM filter only");
  } else {
    debugLogf("[gps] probe saw: %s\n", seen);
  }

  // Want GGA + RMC + ZDA. RMC backs TinyGPS date/time; ZDA is preferred when
  // present. Strip GLL/GSA/GSV/VTG noise. (GGA-only+ZDA without RMC was a
  // lock failure mode when ZDA did not flow after PCAS.)
  constexpr uint16_t kWant =
      (1u << 0) | (1u << 4) | (1u << 6);  // GGA | RMC | ZDA
  constexpr uint16_t kExtras =
      (1u << 1) | (1u << 2) | (1u << 3) | (1u << 5);  // GLL|GSA|GSV|VTG
  const bool alreadyOk =
      n > 0 && (nmeaSeenMask_ & kWant) == kWant && (nmeaSeenMask_ & kExtras) == 0;
  if (alreadyOk) {
    nmeaFilterApplied_ = true;
    debugLogf("[gps] NMEA filter: already GGA+RMC+ZDA — skip PCAS (no FLASH write)");
    return;
  }

  // CASIC PCAS03: GGA,GLL,GSA,GSV,RMC,VTG,ZDA,...
  sendPcas("PCAS03,1,0,0,0,1,0,1,0,0,0,,,0,0");
  delay(GPS_NMEA_CMD_GAP_MS);

  // Persist only when probe observed a wrong/incomplete set. Empty probe →
  // RAM-only this boot (avoid FLASH wear while the module is still waking).
  const bool shouldPersist =
      n > 0 && ((nmeaSeenMask_ & kExtras) != 0 || (nmeaSeenMask_ & kWant) != kWant);
  if (shouldPersist) {
    sendPcas("PCAS00");  // save to module FLASH once
    delay(GPS_NMEA_CMD_GAP_MS);
    debugLogf("[gps] NMEA filter: GGA+RMC+ZDA (saved)");
  } else {
    debugLogf("[gps] NMEA filter: GGA+RMC+ZDA (RAM, not saved)");
  }
  nmeaFilterApplied_ = true;
}

void GpsService::setTempComp(bool enabled, int16_t coeffCenti) {
  localClock_.setTempComp(enabled, coeffCenti);
}


void GpsService::sampleDieTemp() {
  if (!tempSensorOk_) {
    // Lazy re-arm: the first temperatureRead() after boot can transiently
    // return a bad value on S3; retry the probe every 5 s instead of
    // killing temperature for the whole power cycle.
    const uint32_t nowMs = millis();
    if (lastTempTryMs_ != 0 && (nowMs - lastTempTryMs_) < 5000) {
      return;
    }
    lastTempTryMs_ = nowMs;
    tempSensorOk_ = boardTempBegin();
    if (tempSensorOk_) {
      debugLogf("[gps] tsens probe recovered");
    }
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

#if GPS_PPS_RMT_EN
  tryArmRmtAfterFirstPps();
#endif

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
    const uint64_t holdNs = static_cast<uint64_t>(GPS_PPS_RMT_HOLD_MS) * 1000000ULL;
    size_t out = 0;
    for (size_t i = 0; i < npend; ++i) {
      int64_t refined = -1;
      size_t hit = nref;
      for (size_t k = 0; k < nref; ++k) {
        if (ref[k].count == pend[i].count) {
          refined = static_cast<int64_t>(ref[k].edgeUs);
          hit = k;
          break;
        }
        const int64_t dt =
            static_cast<int64_t>(ref[k].edgeUs) - static_cast<int64_t>(pend[i].edgeUs);
        if (dt >= -static_cast<int64_t>(GPS_PPS_RMT_MATCH_US) &&
            dt <= static_cast<int64_t>(GPS_PPS_RMT_MATCH_US)) {
          // Prefer closest in time when count drifted (DMA/race).
          if (refined < 0 ||
              llabs(dt) < llabs(refined - static_cast<int64_t>(pend[i].edgeUs))) {
            refined = static_cast<int64_t>(ref[k].edgeUs);
            hit = k;
          }
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
        if (hit < nref) {
          gRmt.lastWidthUs = ref[hit].widthUs;
        }
        drainedCount = pend[i].count;
      } else if ((nowUs - pend[i].queuedUs) > holdNs) {
        localClock_.onPpsEdge(pend[i].edgeUs, pend[i].count);  // no refinement: GPIO fallback
        // Count unrefined GPIO emits even before first active (diagnose hold misses).
        ++gRmt.fallbacks;
        if (gRmt.active) {
          gRmt.active = false;
        }
        if (!gRmt.reportedFallback) {
          debugLogf("[pps-rmt] stale -> GPIO fallback");
          gRmt.reportedFallback = true;
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
  const bool zdaFresh = zdaValid_ && (millis() - zdaMs_) <= GPS_FIX_MAX_AGE_MS;
  const bool rmcTimeFresh = gps_.date.isValid() && gps_.time.isValid() &&
                            gps_.time.age() <= GPS_FIX_MAX_AGE_MS;
  const bool timeFresh = zdaFresh || rmcTimeFresh;
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
  work.ppsRmt.idfOk = gIdf.ok;
  work.ppsRmt.idfFrames = gIdf.frames;
  work.ppsRmt.idfFirstSyms = gIdf.firstSyms;
  work.ppsRmt.idfLastSyms = gIdf.lastSyms;
  work.ppsRmt.idfLastD0Us = gIdf.lastD0Us;
  work.ppsRmt.idfLastD1Us = gIdf.lastD1Us;
  work.ppsRmt.idfEmptyFrames = gIdf.emptyFrames;
  work.ppsRmt.idfDataFrames = gIdf.dataFrames;
  work.ppsRmt.idfJunkFrames = gIdf.junkFrames;
  work.ppsRmt.idfRawStatus = gIdf.lastRawVal;
  work.ppsRmt.idfStage = gIdf.stage;
  work.ppsRmt.idfErr = gIdf.err;
#endif

  if (zdaFresh) {
    work.ageMs = millis() - zdaMs_;
    if (zdaEpoch_ != lastCommittedSecond_) {
      commitNmeaTime(zdaEpoch_, policy, holdoverSec);
      lastCommittedSecond_ = zdaEpoch_;
    }
  } else if (gps_.date.isValid() && gps_.time.isValid()) {
    TinyGPSDate d = gps_.date;
    TinyGPSTime t = gps_.time;
    const uint32_t epoch =
        civilToEpoch(d.year(), d.month(), d.day(), t.hour(), t.minute(), t.second());
    work.ageMs = gps_.time.age();
    if (epoch != lastCommittedSecond_) {
      commitNmeaTime(epoch, policy, holdoverSec);
      lastCommittedSecond_ = epoch;
    }
  } else {
    work.ageMs = 0xFFFFFFFF;
  }

  const bool nmeaFresh = haveCommit_ && (millis() - commitMs_) <= 3000;
  gExtClock.poll();
  localClock_.setExtAssist(gExtClock.healthy(), gExtClock.ppmFloorHint());
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
  work.extClockEnabled = gExtClock.enabled();
  work.extClockHealthy = gExtClock.healthy();
  work.extClockPpmFloor = gExtClock.ppmFloorHint();
  work.extClockTempC = gExtClock.dieTempC();
  work.extClockDriver = gExtClock.driver();

  publishStatus(work);
  clockTraceMaybeSample(work);

#if GPS_LOCK_DIAG_MS > 0
  if (!work.timeValid) {
    static uint32_t lastDiagMs = 0;
    const uint32_t nowDiag = millis();
    if (lastDiagMs == 0 || (nowDiag - lastDiagMs) >= GPS_LOCK_DIAG_MS) {
      lastDiagMs = nowDiag;
      debugLogf(
          "[clk] wait tv=0 clk=%s pps=%u fresh=%d nmea=%d zda=%d rmc=%d "
          "anchor=%d stable=%d r=%ld commit=%lu age=%lu\n",
          clockStateLabel(work.clockState), work.ppsCount, work.ppsFresh ? 1 : 0,
          nmeaFresh ? 1 : 0, zdaFresh ? 1 : 0, rmcTimeFresh ? 1 : 0,
          localClock_.hasAnchor() ? 1 : 0, localClock_.ppsStable() ? 1 : 0,
          static_cast<long>(work.residualMs),
          static_cast<unsigned long>(commitEpoch_),
          static_cast<unsigned long>(work.ageMs == 0xFFFFFFFFu ? 0 : work.ageMs));
    }
  }
#endif

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
    feedNmeaChar(c);
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
