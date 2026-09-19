#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "config.h"

struct GpsStatus;

// Device-side clock sample ring (PSRAM on S3, DRAM fallback on C3).
// Record while Recording; HTTP fetch only allowed after Stop.
// Spec: docs/clock_trace.md

enum class ClockTraceState : uint8_t {
  Idle = 0,
  Recording = 1,
  Stopped = 2,
};

#pragma pack(push, 1)
struct ClockTraceSample {
  uint32_t seq;
  uint32_t uptimeMs;
  uint32_t utcEpoch;
  uint32_t ppsCount;
  int32_t residualMs;
  uint32_t holdoverMs;
  float freqPpm;
  float tempC;
  float tempCorrPpm;
  uint16_t qualityMs;
  uint8_t state;
  uint8_t flags;  // bit0 ppsFresh, bit1 timeValid, bit2 tempComp
  uint8_t satellites;
  uint8_t reserved;
};

// On-wire page header for GET /debug/clock/data?format=bin (little-endian).
struct ClockTraceBinHeader {
  char magic[4];       // 'C','T','R','B'
  uint16_t version;    // 1
  uint16_t sampleSize; // sizeof(ClockTraceSample)
  uint32_t seqFrom;
  uint32_t count;      // samples in this page
  uint32_t seqNext;    // next seq after this page
  uint32_t seqEnd;     // session end (info.seqNext)
  uint32_t dropped;
  uint32_t flags;      // bit0 = done (seqNext >= seqEnd)
};
#pragma pack(pop)

static_assert(sizeof(ClockTraceSample) == 42, "ClockTraceSample size");
static_assert(sizeof(ClockTraceBinHeader) == 32, "ClockTraceBinHeader size");

struct ClockTraceInfo {
  ClockTraceState state = ClockTraceState::Idle;
  uint32_t capacity = 0;
  uint32_t count = 0;
  uint32_t dropped = 0;
  uint32_t seqFirst = 0;
  uint32_t seqNext = 0;
  uint32_t startedMs = 0;
  uint32_t stoppedMs = 0;
  bool psram = false;
  bool available = false;
};

#if CLOCK_TRACE_EN

void clockTraceBegin();
void clockTraceMaybeSample(const GpsStatus& st);
bool clockTraceStart(char* err, size_t errCap);
bool clockTraceStop(char* err, size_t errCap);
bool clockTraceClear(char* err, size_t errCap);
ClockTraceInfo clockTraceInfo();
const char* clockTraceStateLabel(ClockTraceState s);
size_t clockTraceRead(uint32_t fromSeq, ClockTraceSample* out, size_t maxOut,
                      uint32_t* nextSeqOut, int* errCode);

#else

static inline void clockTraceBegin() {}
static inline void clockTraceMaybeSample(const GpsStatus&) {}
static inline bool clockTraceStart(char* err, size_t errCap) {
  if (err && errCap) {
    snprintf(err, errCap, "CLOCK_TRACE_EN=0");
  }
  return false;
}
static inline bool clockTraceStop(char* err, size_t errCap) {
  if (err && errCap) {
    snprintf(err, errCap, "CLOCK_TRACE_EN=0");
  }
  return false;
}
static inline bool clockTraceClear(char* err, size_t errCap) {
  if (err && errCap) {
    snprintf(err, errCap, "CLOCK_TRACE_EN=0");
  }
  return false;
}
static inline ClockTraceInfo clockTraceInfo() { return {}; }
static inline const char* clockTraceStateLabel(ClockTraceState) { return "OFF"; }
static inline size_t clockTraceRead(uint32_t, ClockTraceSample*, size_t, uint32_t*, int* errCode) {
  if (errCode) {
    *errCode = 1;
  }
  return 0;
}

#endif
