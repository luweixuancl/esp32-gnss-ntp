#pragma once

#include <stddef.h>
#include <stdint.h>

// RAM ring of tagged boot/runtime lines, teed to Serial. Fetch after WiFi via
// GET /debug/log (session cookie or ?pass=). See docs/debug_log.md.

#ifndef DEBUG_LOG_EN
#define DEBUG_LOG_EN 1
#endif

#ifndef DEBUG_LOG_BYTES
#define DEBUG_LOG_BYTES (32 * 1024)
#endif

#if DEBUG_LOG_EN
void debugLogBegin();
void debugLogf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void debugLogClear();
size_t debugLogUsed();
uint32_t debugLogDropped();
// Linearize ring into out (NUL-terminated if outCap>0). Returns bytes written
// excluding NUL. Sets *droppedOut if non-null.
size_t debugLogSnapshot(char* out, size_t outCap, uint32_t* droppedOut);
#else
static inline void debugLogBegin() {}
static inline void debugLogf(const char* /*fmt*/, ...) {}
static inline void debugLogClear() {}
static inline size_t debugLogUsed() { return 0; }
static inline uint32_t debugLogDropped() { return 0; }
static inline size_t debugLogSnapshot(char* out, size_t outCap, uint32_t* droppedOut) {
  if (droppedOut) {
    *droppedOut = 0;
  }
  if (out && outCap) {
    out[0] = '\0';
  }
  return 0;
}
#endif
