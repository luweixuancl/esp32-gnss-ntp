#include "status_leds.h"
#include <Arduino.h>
#include "app_ipc.h"
#include "config.h"

#if defined(ARDUINO_ESP32S3_DEV)
// Three logical channels share the onboard SK6812-mini (GPIO48): R = network
// (D4 semantics), G = clock/GNSS (D5 semantics), B = NTP serving-ready
// (steady while Locked/Degraded/Holdover, off otherwise). OTA overrides all
// three with dedicated colours from the AppIpc OTA snapshot.
static bool rgbD4On_ = false;
static bool rgbD5On_ = false;
static bool rgbBOn_ = false;

static void flushRgb() {
#if GPS_PPS_RMT_EN
  // v1.1.35: onboard RGB uses RMT TX @10 MHz; sharing the RMT group with PPS
  // RX left every frame as 1×zero-duration symbols (see v1.1.34 dump). Skip
  // rgbLedWrite entirely while PPS RMT board builds are enabled.
  return;
#else
  const uint8_t v = LED_RGB_BRIGHTNESS;
  // Arduino-ESP32 3.x: neopixelWrite() is deprecated.
  rgbLedWrite(PIN_LED_RGB, rgbD4On_ ? v : 0, rgbD5On_ ? v : 0, rgbBOn_ ? v : 0);
#endif
}

static void setRgb(bool r, bool g, bool b) {
  // Skip RMT TX when the pattern is unchanged (heartbeat still toggles).
  if (r == rgbD4On_ && g == rgbD5On_ && b == rgbBOn_) {
    return;
  }
  rgbD4On_ = r;
  rgbD5On_ = g;
  rgbBOn_ = b;
  flushRgb();
}
#else
static void flushRgb() {
}
static void setRgb(bool /*r*/, bool /*g*/, bool /*b*/) {
}
#endif

static void setLed(uint8_t pin, bool on) {
#if defined(ARDUINO_ESP32S3_DEV)
  if (pin == PIN_LED_D4) {
    if (rgbD4On_ == on) {
      return;
    }
    rgbD4On_ = on;
  } else {
    if (rgbD5On_ == on) {
      return;
    }
    rgbD5On_ = on;
  }
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

void StatusLeds::begin() {
#if defined(ARDUINO_ESP32S3_DEV)
  flushRgb();
#else
  pinMode(PIN_LED_D4, OUTPUT);
  pinMode(PIN_LED_D5, OUTPUT);
  digitalWrite(PIN_LED_D4, LOW);
  digitalWrite(PIN_LED_D5, LOW);
#endif
}

void StatusLeds::writeBlink(uint8_t pin, uint32_t nowMs, uint32_t halfPeriodMs) {
  const bool on = ((nowMs / halfPeriodMs) % 2) == 0;
  setLed(pin, on);
}

void StatusLeds::writeHeartbeat(uint8_t pin, uint32_t nowMs) {
  const uint32_t period = LED_HEARTBEAT_ON_MS + LED_HEARTBEAT_OFF_MS;
  const bool on = (nowMs % period) < LED_HEARTBEAT_ON_MS;
  setLed(pin, on);
}

bool StatusLeds::writeOtaPattern(uint32_t nowMs) {
  // Observe IPC snapshot only — no dependency on OtaService.
  const OtaPhase phase = ipcOtaPhase();
  if (phase == OtaPhase::Idle) {
    return false;
  }
  const bool on = ((nowMs / OTA_LED_BLINK_HALF_MS) % 2) == 0;
  switch (phase) {
    case OtaPhase::Uploading:
#if defined(ARDUINO_ESP32S3_DEV)
      setRgb(on, on, false);  // amber
#else
      setLed(PIN_LED_D4, on);
      setLed(PIN_LED_D5, on);
#endif
      return true;
    case OtaPhase::Rebooting:
#if defined(ARDUINO_ESP32S3_DEV)
      setRgb(false, true, false);  // solid green
#else
      setLed(PIN_LED_D4, false);
      setLed(PIN_LED_D5, true);
#endif
      return true;
    case OtaPhase::Failed:
#if defined(ARDUINO_ESP32S3_DEV)
      setRgb(on, false, false);  // red blink
#else
      setLed(PIN_LED_D4, on);
      setLed(PIN_LED_D5, false);
#endif
      return true;
    case OtaPhase::Idle:
    default:
      return false;
  }
}

bool StatusLeds::tasksStale(uint32_t nowMs) {
  // OTA blocks task-net inside handleClient; IPC busy + kickNetAlive cover it.
  // Pending-verify: never panic-restart — that would roll back a fresh OTA.
  if (ipcOtaBusy() || ipcOtaPendingVerify()) {
    return false;
  }
  const uint32_t t = gIpc.kickTimeMs;
  const uint32_t n = gIpc.kickNetMs;
  const uint32_t u = gIpc.kickUiMs;
  if (t == 0 || n == 0 || u == 0) {
    return false;
  }
  auto aged = [nowMs](uint32_t kick) -> bool {
    return (nowMs - kick) > LED_TASK_STALE_MS;
  };
  return aged(t) || aged(n) || aged(u);
}

void StatusLeds::loop(bool apMode, bool wifiStaOk, const GpsStatus& st) {
  const uint32_t now = millis();
  if (now - lastUpdateMs_ < 20) {
    return;
  }
  lastUpdateMs_ = now;

  if (writeOtaPattern(now)) {
    panicSinceMs_ = 0;
    return;
  }

  if (tasksStale(now)) {
    if (panicSinceMs_ == 0) {
      panicSinceMs_ = now;
    } else if ((now - panicSinceMs_) >= LED_TASK_PANIC_RESTART_MS) {
      Serial.println("[leds] task stale too long — restart");
      delay(50);
      ESP.restart();
    }
    const bool phase = ((now / LED_PANIC_HALF_PERIOD_MS) % 2) == 0;
    setLed(PIN_LED_D4, phase);
    setLed(PIN_LED_D5, !phase);
#if defined(ARDUINO_ESP32S3_DEV)
    rgbBOn_ = false;
#endif
    flushRgb();
    return;
  }
  panicSinceMs_ = 0;

  if (wifiStaOk) {
    writeHeartbeat(PIN_LED_D4, now);
  } else if (apMode) {
    writeBlink(PIN_LED_D4, now, 120);
  } else {
    writeBlink(PIN_LED_D4, now, 500);
  }

  if (st.validFix && st.ppsFresh && st.timeValid &&
      (st.clockState == ClockState::Locked || st.clockState == ClockState::Degraded)) {
    writeHeartbeat(PIN_LED_D5, now + LED_HEARTBEAT_ON_MS / 2);
  } else if (st.clockState == ClockState::Holdover) {
    writeBlink(PIN_LED_D5, now, 200);
  } else if (st.validFix || st.satellites > 0 || st.clockState == ClockState::Acquiring) {
    writeBlink(PIN_LED_D5, now, 400);
  } else {
    setLed(PIN_LED_D5, false);
  }

#if defined(ARDUINO_ESP32S3_DEV)
  rgbBOn_ = (st.clockState == ClockState::Locked ||
             st.clockState == ClockState::Degraded ||
             st.clockState == ClockState::Holdover);
#endif
  flushRgb();
}
