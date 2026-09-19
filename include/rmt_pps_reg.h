#pragma once

// Register-level RMT RX driver for GNSS 1PPS (ESP32-S3).
// Bypasses Arduino/IDF legacy `driver/rmt.h` — programs RMT via
// `soc/rmt_struct.h` + `hal/rmt_ll.h` per TRM Ch.37.
//
// Scope: complete PPS capture path (clock, GPIO matrix, filter, idle end,
// ISR, RAM read, edge reconstruct, re-arm). Not a full IR RMT stack.
//
// Enable with GPS_PPS_RMT_REG_EN=1 (S3 only; no-op elsewhere).

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef GPS_PPS_RMT_REG_EN
#define GPS_PPS_RMT_REG_EN 0
#endif

struct RmtPpsRegEdge {
  uint64_t edgeUs;     // reconstructed rising-edge time (esp_timer µs)
  uint32_t widthUs;    // first high pulse width
  uint32_t symbols;    // non-zero pulse codes in the frame
  uint32_t isrLatUs;   // approx ISR latency pad (idle_thres ticks)
};

struct RmtPpsRegStats {
  bool armed;
  uint32_t frames;
  uint32_t dataFrames;
  uint32_t emptyFrames;
  uint32_t overflows;
  uint32_t ownerErr;
  uint32_t lastWidthUs;
  uint32_t lastSymbols;
  uint32_t lastStatus;
  uint8_t rxChannel;   // LL RX index 0..3 (HW ch 4..7)
};

bool rmtPpsRegBegin(int gpioNum);
void rmtPpsRegEnd();
bool rmtPpsRegArmed();
bool rmtPpsRegPop(RmtPpsRegEdge* out);
void rmtPpsRegGetStats(RmtPpsRegStats* out);
