#include "clock_trace.h"

#if CLOCK_TRACE_EN

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "debug_log.h"
#include "gps_service.h"

namespace {

ClockTraceSample* gBuf = nullptr;
uint32_t gCap = 0;
uint32_t gHead = 0;   // index of oldest
uint32_t gCount = 0;
uint32_t gDropped = 0;
uint32_t gNextSeq = 0;
uint32_t gStartedMs = 0;
uint32_t gStoppedMs = 0;
uint32_t gLastPps = 0xFFFFFFFFu;
ClockTraceState gState = ClockTraceState::Idle;
bool gPsram = false;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

bool allocBuf(uint32_t cap, bool* usedPsram) {
  if (gBuf != nullptr) {
    if (usedPsram) {
      *usedPsram = gPsram;
    }
    return gCap >= cap;
  }
  const size_t bytes = static_cast<size_t>(cap) * sizeof(ClockTraceSample);
  void* p = nullptr;
#if defined(BOARD_HAS_PSRAM)
  p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p != nullptr) {
    gPsram = true;
  }
#endif
  if (p == nullptr) {
    p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    gPsram = false;
  }
  if (p == nullptr) {
    return false;
  }
  gBuf = static_cast<ClockTraceSample*>(p);
  gCap = cap;
  if (usedPsram) {
    *usedPsram = gPsram;
  }
  return true;
}

void freeBufUnlocked() {
  if (gBuf != nullptr) {
    heap_caps_free(gBuf);
    gBuf = nullptr;
  }
  gCap = 0;
  gHead = 0;
  gCount = 0;
  gDropped = 0;
  gNextSeq = 0;
  gStartedMs = 0;
  gStoppedMs = 0;
  gLastPps = 0xFFFFFFFFu;
  gPsram = false;
  gState = ClockTraceState::Idle;
}

void pushUnlocked(const ClockTraceSample& s) {
  if (gBuf == nullptr || gCap == 0) {
    return;
  }
  if (gCount < gCap) {
    const uint32_t idx = (gHead + gCount) % gCap;
    gBuf[idx] = s;
    ++gCount;
  } else {
    // Overwrite oldest.
    gBuf[gHead] = s;
    gHead = (gHead + 1) % gCap;
    ++gDropped;
  }
  gNextSeq = s.seq + 1;
}

}  // namespace

const char* clockTraceStateLabel(ClockTraceState s) {
  switch (s) {
    case ClockTraceState::Recording:
      return "REC";
    case ClockTraceState::Stopped:
      return "STOP";
    case ClockTraceState::Idle:
    default:
      return "IDLE";
  }
}

void clockTraceBegin() {
  // Lazy alloc on start — keep boot heap free.
}

bool clockTraceStart(char* err, size_t errCap) {
  portENTER_CRITICAL(&gMux);
  if (gState == ClockTraceState::Recording) {
    portEXIT_CRITICAL(&gMux);
    if (err && errCap) {
      snprintf(err, errCap, "already recording");
    }
    return false;
  }
  // Fresh session: drop any previous Stopped buffer.
  freeBufUnlocked();
  bool psram = false;
  if (!allocBuf(CLOCK_TRACE_CAP, &psram)) {
    portEXIT_CRITICAL(&gMux);
    if (err && errCap) {
      snprintf(err, errCap, "alloc %u samples failed", static_cast<unsigned>(CLOCK_TRACE_CAP));
    }
    return false;
  }
  gState = ClockTraceState::Recording;
  gStartedMs = millis();
  gStoppedMs = 0;
  gDropped = 0;
  gNextSeq = 0;
  gLastPps = 0xFFFFFFFFu;
  portEXIT_CRITICAL(&gMux);
  debugLogf("[clock-trace] START cap=%u psram=%d", static_cast<unsigned>(CLOCK_TRACE_CAP),
            psram ? 1 : 0);
  if (err && errCap) {
    err[0] = '\0';
  }
  return true;
}

bool clockTraceStop(char* err, size_t errCap) {
  portENTER_CRITICAL(&gMux);
  if (gState != ClockTraceState::Recording) {
    const bool ok = (gState == ClockTraceState::Stopped);
    portEXIT_CRITICAL(&gMux);
    if (!ok && err && errCap) {
      snprintf(err, errCap, "not recording");
    }
    return ok;
  }
  gState = ClockTraceState::Stopped;
  gStoppedMs = millis();
  const uint32_t n = gCount;
  const uint32_t drop = gDropped;
  portEXIT_CRITICAL(&gMux);
  debugLogf("[clock-trace] STOP count=%u dropped=%u", static_cast<unsigned>(n),
            static_cast<unsigned>(drop));
  if (err && errCap) {
    err[0] = '\0';
  }
  return true;
}

bool clockTraceClear(char* err, size_t errCap) {
  portENTER_CRITICAL(&gMux);
  if (gState == ClockTraceState::Recording) {
    portEXIT_CRITICAL(&gMux);
    if (err && errCap) {
      snprintf(err, errCap, "stop before clear");
    }
    return false;
  }
  freeBufUnlocked();
  portEXIT_CRITICAL(&gMux);
  debugLogf("[clock-trace] CLEAR");
  if (err && errCap) {
    err[0] = '\0';
  }
  return true;
}

ClockTraceInfo clockTraceInfo() {
  ClockTraceInfo info;
  portENTER_CRITICAL(&gMux);
  info.state = gState;
  info.capacity = gCap;
  info.count = gCount;
  info.dropped = gDropped;
  info.seqNext = gNextSeq;
  if (gCount > 0) {
    info.seqFirst = gNextSeq - gCount;
  } else {
    info.seqFirst = 0;
  }
  info.startedMs = gStartedMs;
  info.stoppedMs = gStoppedMs;
  info.psram = gPsram;
  info.available = (gBuf != nullptr) || (gState == ClockTraceState::Idle);
  portEXIT_CRITICAL(&gMux);
  return info;
}

void clockTraceMaybeSample(const GpsStatus& st) {
  if (gState != ClockTraceState::Recording) {
    return;
  }
  // One sample per PPS edge (natural ~1 Hz). Skip duplicate counts.
  if (st.ppsCount == 0 || st.ppsCount == gLastPps) {
    return;
  }

  ClockTraceSample s{};
  s.uptimeMs = millis();
  s.utcEpoch = st.utcEpoch;
  s.ppsCount = st.ppsCount;
  s.residualMs = st.residualMs;
  s.holdoverMs = st.holdoverMs;
  s.freqPpm = st.freqPpm;
  s.tempC = st.tempC;
  s.tempCorrPpm = st.tempCorrPpm;
  s.qualityMs = (st.qualityMs > 65535u) ? 65535u : static_cast<uint16_t>(st.qualityMs);
  s.state = static_cast<uint8_t>(st.clockState);
  s.flags = 0;
  if (st.ppsFresh) {
    s.flags |= 0x01;
  }
  if (st.timeValid) {
    s.flags |= 0x02;
  }
  if (st.tempComp) {
    s.flags |= 0x04;
  }
  s.satellites = st.satellites;
  s.reserved = 0;

  portENTER_CRITICAL(&gMux);
  if (gState != ClockTraceState::Recording) {
    portEXIT_CRITICAL(&gMux);
    return;
  }
  gLastPps = st.ppsCount;
  s.seq = gNextSeq;
  pushUnlocked(s);
  portEXIT_CRITICAL(&gMux);
}

size_t clockTraceRead(uint32_t fromSeq, ClockTraceSample* out, size_t maxOut,
                      uint32_t* nextSeqOut, int* errCode) {
  if (out == nullptr || maxOut == 0) {
    if (errCode) {
      *errCode = 2;
    }
    return 0;
  }
  portENTER_CRITICAL(&gMux);
  if (gState != ClockTraceState::Stopped || gBuf == nullptr || gCount == 0) {
    portEXIT_CRITICAL(&gMux);
    if (errCode) {
      *errCode = (gState != ClockTraceState::Stopped) ? 1 : 2;
    }
    if (nextSeqOut) {
      *nextSeqOut = fromSeq;
    }
    return 0;
  }
  const uint32_t seqFirst = gNextSeq - gCount;
  const uint32_t seqNext = gNextSeq;
  if (fromSeq < seqFirst) {
    fromSeq = seqFirst;
  }
  if (fromSeq >= seqNext) {
    portEXIT_CRITICAL(&gMux);
    if (errCode) {
      *errCode = 0;
    }
    if (nextSeqOut) {
      *nextSeqOut = seqNext;
    }
    return 0;
  }
  size_t n = 0;
  uint32_t seq = fromSeq;
  while (n < maxOut && seq < seqNext) {
    const uint32_t off = seq - seqFirst;
    const uint32_t idx = (gHead + off) % gCap;
    out[n++] = gBuf[idx];
    ++seq;
  }
  portEXIT_CRITICAL(&gMux);
  if (errCode) {
    *errCode = 0;
  }
  if (nextSeqOut) {
    *nextSeqOut = seq;
  }
  return n;
}

#endif  // CLOCK_TRACE_EN
