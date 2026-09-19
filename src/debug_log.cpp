#include "debug_log.h"

#if DEBUG_LOG_EN

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static char gBuf[DEBUG_LOG_BYTES];
static size_t gStart = 0;  // oldest byte index
static size_t gUsed = 0;
static uint32_t gDropped = 0;
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static bool gReady = false;

void debugLogBegin() {
  portENTER_CRITICAL(&gMux);
  gStart = 0;
  gUsed = 0;
  gDropped = 0;
  gReady = true;
  portEXIT_CRITICAL(&gMux);
}

static void appendUnlocked(const char* data, size_t n) {
  if (n == 0 || !gReady) {
    return;
  }
  if (n >= DEBUG_LOG_BYTES) {
    // Keep only the tail that fits.
    data += (n - (DEBUG_LOG_BYTES - 1));
    n = DEBUG_LOG_BYTES - 1;
    gStart = 0;
    gUsed = 0;
    gDropped += static_cast<uint32_t>(n);  // approximate; best-effort
  }
  while (gUsed + n > DEBUG_LOG_BYTES) {
    // Drop oldest byte.
    gStart = (gStart + 1) % DEBUG_LOG_BYTES;
    --gUsed;
    ++gDropped;
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t idx = (gStart + gUsed) % DEBUG_LOG_BYTES;
    gBuf[idx] = data[i];
    ++gUsed;
  }
}

void debugLogf(const char* fmt, ...) {
  char line[384];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  size_t len = static_cast<size_t>(n);
  if (len >= sizeof(line)) {
    len = sizeof(line) - 1;
    line[len] = '\0';
  }
  // Ensure newline so ring stays line-oriented for humans.
  bool needNl = (len == 0) || (line[len - 1] != '\n');
  Serial.write(reinterpret_cast<const uint8_t*>(line), len);
  if (needNl) {
    Serial.write('\n');
  }

  portENTER_CRITICAL(&gMux);
  appendUnlocked(line, len);
  if (needNl) {
    const char nl = '\n';
    appendUnlocked(&nl, 1);
  }
  portEXIT_CRITICAL(&gMux);
}

void debugLogClear() {
  portENTER_CRITICAL(&gMux);
  gStart = 0;
  gUsed = 0;
  gDropped = 0;
  portEXIT_CRITICAL(&gMux);
}

size_t debugLogUsed() {
  portENTER_CRITICAL(&gMux);
  const size_t u = gUsed;
  portEXIT_CRITICAL(&gMux);
  return u;
}

uint32_t debugLogDropped() {
  portENTER_CRITICAL(&gMux);
  const uint32_t d = gDropped;
  portEXIT_CRITICAL(&gMux);
  return d;
}

size_t debugLogSnapshot(char* out, size_t outCap, uint32_t* droppedOut) {
  if (out == nullptr || outCap == 0) {
    if (droppedOut) {
      *droppedOut = debugLogDropped();
    }
    return 0;
  }
  portENTER_CRITICAL(&gMux);
  const size_t used = gUsed;
  const size_t start = gStart;
  const uint32_t dropped = gDropped;
  const size_t copyN = (used < outCap - 1) ? used : (outCap - 1);
  for (size_t i = 0; i < copyN; ++i) {
    out[i] = gBuf[(start + i) % DEBUG_LOG_BYTES];
  }
  portEXIT_CRITICAL(&gMux);
  out[copyN] = '\0';
  if (droppedOut) {
    *droppedOut = dropped;
  }
  return copyN;
}

#endif  // DEBUG_LOG_EN
