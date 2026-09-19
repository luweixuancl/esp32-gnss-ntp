#include "rmt_pps_reg.h"
#include "config.h"

#if GPS_PPS_RMT_REG_EN && defined(ARDUINO_ESP32S3_DEV)

#include <Arduino.h>
#include <esp_intr_alloc.h>
#include <esp_timer.h>
#include <soc/rmt_struct.h>
#include <soc/gpio_sig_map.h>
#include <soc/periph_defs.h>
#include <driver/periph_ctrl.h>
#include <driver/gpio.h>
#include <hal/rmt_ll.h>
#include <esp32s3/rom/gpio.h>

// LL RX channel index 0..3 maps to HW channels 4..7 (TRM).
// Prefer HW ch7 (LL index 3): only RX channel with DMA capability.
#ifndef GPS_PPS_RMT_REG_RX_CH
#define GPS_PPS_RMT_REG_RX_CH 3
#endif

// APB ≈ 80 MHz on S3; group clock = APB (sel=1), undivided → 80 MHz sclk.
// Channel div 80 → 1 MHz tick (1 µs), matching GPS_PPS_RMT_TICK_NS.
static constexpr uint8_t kSclkSelApb = 1;
static constexpr uint8_t kChanDiv = 80;
static constexpr uint32_t kTickUs = 1;
static constexpr uint32_t kIdleThresTicks =
    static_cast<uint32_t>(GPS_PPS_RMT_WINDOW_MS) * 1000u / kTickUs;
// Filter threshold is in APB cycles per rmt_struct.h (not channel ticks).
static constexpr uint32_t kFilterApbCycles =
    static_cast<uint32_t>(GPS_PPS_RMT_FILTER_NS) * 80u / 1000u;  // 80 MHz APB

static constexpr uint8_t kQSize = GPS_PPS_RMT_QUEUE;
static constexpr uint8_t kRxCh = GPS_PPS_RMT_REG_RX_CH;

static bool gArmed = false;
static intr_handle_t gIntr = nullptr;
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint8_t gHead = 0;
static volatile uint8_t gTail = 0;
static RmtPpsRegEdge gQ[kQSize];

static volatile uint32_t gFrames = 0;
static volatile uint32_t gDataFrames = 0;
static volatile uint32_t gEmptyFrames = 0;
static volatile uint32_t gOverflows = 0;
static volatile uint32_t gOwnerErr = 0;
static volatile uint32_t gLastWidthUs = 0;
static volatile uint32_t gLastSymbols = 0;
static volatile uint32_t gLastStatus = 0;

static inline uint32_t rxSigIdx(uint8_t llRxCh) {
  switch (llRxCh) {
    case 0: return RMT_SIG_IN0_IDX;
    case 1: return RMT_SIG_IN1_IDX;
    case 2: return RMT_SIG_IN2_IDX;
    default: return RMT_SIG_IN3_IDX;
  }
}

static void IRAM_ATTR rearmRx() {
  rmt_ll_rx_enable(&RMT, kRxCh, false);
  rmt_ll_rx_set_mem_owner(&RMT, kRxCh, 1);  // HW owns
  rmt_ll_rx_reset_pointer(&RMT, kRxCh);
  rmt_ll_rx_enable(&RMT, kRxCh, true);
}

static void IRAM_ATTR pushEdge(const RmtPpsRegEdge& e) {
  const uint8_t next = static_cast<uint8_t>((gHead + 1) % kQSize);
  if (next == gTail) {
    gOverflows++;
    return;
  }
  gQ[gHead] = e;
  gHead = next;
}

static void IRAM_ATTR onRxEnd() {
  const uint64_t nowUs = esp_timer_get_time();
  gFrames++;
  gLastStatus = rmt_ll_rx_get_channel_status(&RMT, kRxCh);

  // Take ownership for APB read of RMTMEM.
  rmt_ll_rx_enable(&RMT, kRxCh, false);
  rmt_ll_rx_set_mem_owner(&RMT, kRxCh, 0);  // APB owns

  // HW channel = 4 + ll index; each channel has 48 words in RMTMEM.
  const uint32_t hwCh = 4u + kRxCh;
  const rmt_item32_t* items = RMTMEM.chan[hwCh].data32;

  uint32_t symbols = 0;
  uint32_t totalTicks = 0;
  uint32_t firstHighUs = 0;
  bool sawHigh = false;

  for (uint32_t i = 0; i < 48; ++i) {
    const rmt_item32_t it = items[i];
    const uint32_t d0 = it.duration0;
    const uint32_t d1 = it.duration1;
    if (d0 == 0 && d1 == 0) {
      break;
    }
    if (d0 != 0) {
      symbols++;
      totalTicks += d0;
      if (!sawHigh && it.level0) {
        firstHighUs = d0 * kTickUs;
        sawHigh = true;
      }
    }
    if (d1 != 0) {
      symbols++;
      totalTicks += d1;
      if (!sawHigh && it.level1) {
        firstHighUs = d1 * kTickUs;
        sawHigh = true;
      }
    } else if (d0 != 0 && d1 == 0) {
      // End-marker in second half after a valid first half.
      break;
    }
  }

  gLastSymbols = symbols;
  gLastWidthUs = firstHighUs;

  if (symbols == 0) {
    gEmptyFrames++;
  } else {
    gDataFrames++;
    // RX_END fires after idle_thres of unchanged level. Rising edge ≈
    // now - idle_window - remaining pulse codes after the edge.
    // Conservative reconstruct: now - idle_thres - firstHigh (if high-idle
    // PPS) — good enough to compare against GPIO ISR until board-tuned.
    const uint32_t idleUs = kIdleThresTicks * kTickUs;
    RmtPpsRegEdge e{};
    e.widthUs = firstHighUs;
    e.symbols = symbols;
    e.isrLatUs = idleUs;
    const uint64_t back = static_cast<uint64_t>(idleUs) +
                          static_cast<uint64_t>(totalTicks) * kTickUs;
    e.edgeUs = (nowUs > back) ? (nowUs - back) : nowUs;
    pushEdge(e);
  }

  if (gLastStatus & (1u << 25)) {  // mem_owner_err_m
    gOwnerErr++;
  }

  rmt_ll_clear_rx_end_interrupt(&RMT, kRxCh);
  rmt_ll_clear_rx_err_interrupt(&RMT, kRxCh);
  rearmRx();
}

static void IRAM_ATTR rmtIsr(void* /*arg*/) {
  const uint32_t rxEnd = rmt_ll_get_rx_end_interrupt_status(&RMT);
  const uint32_t rxErr = rmt_ll_get_rx_err_interrupt_status(&RMT);
  if (rxEnd & (1u << kRxCh)) {
    onRxEnd();
  } else if (rxErr & (1u << kRxCh)) {
    gOwnerErr++;
    rmt_ll_clear_rx_err_interrupt(&RMT, kRxCh);
    rearmRx();
  }
}

bool rmtPpsRegBegin(int gpioNum) {
  if (gArmed) {
    return true;
  }
  if (kRxCh > 3) {
    Serial.println("[pps-rmt-reg] bad RX channel");
    return false;
  }

  periph_module_enable(PERIPH_RMT_MODULE);

  // Shared RMT block also drives S3 RGB (TX). Enable clocks; only set group
  // source if not already active so we do not stomp the LED path.
  rmt_ll_enable_drive_clock(&RMT, true);
  rmt_ll_power_down_mem(&RMT, false);
  rmt_ll_enable_mem_access(&RMT, true);  // apb_fifo_mask=1 → direct RMTMEM
  if (!RMT.sys_conf.sclk_active) {
    rmt_ll_set_group_clock_src(&RMT, 0, kSclkSelApb, /*div_num=*/0, 0, 1);
  }

  // GPIO matrix → RMT_SIG_INn
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << gpioNum;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  gpio_matrix_in(static_cast<uint32_t>(gpioNum), rxSigIdx(kRxCh), false);

  // Channel config
  rmt_ll_rx_enable(&RMT, kRxCh, false);
  rmt_ll_rx_set_channel_clock_div(&RMT, kRxCh, kChanDiv);
  rmt_ll_rx_reset_channel_clock_div(&RMT, kRxCh);
  rmt_ll_rx_set_mem_blocks(&RMT, kRxCh, 1);
  rmt_ll_rx_set_idle_thres(&RMT, kRxCh, kIdleThresTicks > 0x7FFF ? 0x7FFF : kIdleThresTicks);
  rmt_ll_rx_enable_filter(&RMT, kRxCh, true);
  rmt_ll_rx_set_filter_thres(&RMT, kRxCh, kFilterApbCycles > 255 ? 255 : kFilterApbCycles);
  rmt_ll_rx_enable_carrier_demodulation(&RMT, kRxCh, false);
  rmt_ll_rx_enable_pingpong(&RMT, kRxCh, false);
  rmt_ll_rx_set_mem_owner(&RMT, kRxCh, 1);
  rmt_ll_rx_reset_pointer(&RMT, kRxCh);

  // Conf update latch
  RMT.chmconf[kRxCh].conf1.conf_update_m = 1;

  esp_err_t err = esp_intr_alloc(ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_IRAM, rmtIsr, nullptr, &gIntr);
  if (err != ESP_OK) {
    Serial.printf("[pps-rmt-reg] intr alloc failed %d\n", static_cast<int>(err));
    return false;
  }
  rmt_ll_enable_rx_end_interrupt(&RMT, kRxCh, true);
  rmt_ll_enable_rx_err_interrupt(&RMT, kRxCh, true);

  rmt_ll_rx_enable(&RMT, kRxCh, true);
  gArmed = true;
  Serial.printf("[pps-rmt-reg] armed gpio=%d llRx=%u hwCh=%u tick=1us idle=%uus filter=%uapb\n",
                gpioNum, kRxCh, 4u + kRxCh, static_cast<unsigned>(kIdleThresTicks),
                static_cast<unsigned>(kFilterApbCycles));
  return true;
}

void rmtPpsRegEnd() {
  if (!gArmed) {
    return;
  }
  rmt_ll_rx_enable(&RMT, kRxCh, false);
  rmt_ll_enable_rx_end_interrupt(&RMT, kRxCh, false);
  rmt_ll_enable_rx_err_interrupt(&RMT, kRxCh, false);
  if (gIntr) {
    esp_intr_free(gIntr);
    gIntr = nullptr;
  }
  gArmed = false;
}

bool rmtPpsRegArmed() { return gArmed; }

bool rmtPpsRegPop(RmtPpsRegEdge* out) {
  if (out == nullptr || gTail == gHead) {
    return false;
  }
  portENTER_CRITICAL(&gMux);
  *out = gQ[gTail];
  gTail = static_cast<uint8_t>((gTail + 1) % kQSize);
  portEXIT_CRITICAL(&gMux);
  return true;
}

void rmtPpsRegGetStats(RmtPpsRegStats* out) {
  if (out == nullptr) {
    return;
  }
  out->armed = gArmed;
  out->frames = gFrames;
  out->dataFrames = gDataFrames;
  out->emptyFrames = gEmptyFrames;
  out->overflows = gOverflows;
  out->ownerErr = gOwnerErr;
  out->lastWidthUs = gLastWidthUs;
  out->lastSymbols = gLastSymbols;
  out->lastStatus = gLastStatus;
  out->rxChannel = kRxCh;
}

#else  // !REG_EN or not S3

bool rmtPpsRegBegin(int) { return false; }
void rmtPpsRegEnd() {}
bool rmtPpsRegArmed() { return false; }
bool rmtPpsRegPop(RmtPpsRegEdge*) { return false; }
void rmtPpsRegGetStats(RmtPpsRegStats* out) {
  if (out) {
    *out = {};
  }
}

#endif
