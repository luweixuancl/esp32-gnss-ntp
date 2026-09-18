#pragma once

// Firmware identity — bump PATCH (or MINOR) on every flashable build so the
// OLED mark is unambiguous after OTA / serial upgrade.
#define FW_VER_MAJOR         1
#define FW_VER_MINOR         1
#define FW_VER_PATCH         16
// Human mark on OLED home + boot splash (easy to eyeball: "v1.1.16").
#define FW_MARK              "v1.1.16"
// Full string for /status, /cfg, serial, OTA pages.
#define FW_VERSION           "1.1.16"
// After a pending-verify OTA boot, wait until tasks are alive this long before
// cancelling rollback — catches crash-loops in GPS/WiFi/task bring-up.
#define OTA_MARK_VALID_AFTER_MS  30000
// Progress log cadence during Web OTA (bytes).
#define OTA_PROGRESS_LOG_BYTES   (64 * 1024)
// Failed-OTA LED linger before returning to normal status colours.
#define OTA_FAIL_LED_MS          2000
// While OTA busy: task-time yields this long so flash/WiFi serve the upload.
#define OTA_TIME_TASK_YIELD_MS     50
// S3 RGB / C3 dual-LED OTA blink half-period (amber upload ~4 Hz).
#define OTA_LED_BLINK_HALF_MS     120

// FreeRTOS task priorities (composition root creates tasks at normal values;
// OtaService temporarily boosts net above time during upload).
#define TASK_PRIO_TIME              5
#define TASK_PRIO_NET               2
#define TASK_PRIO_UI                1
#define TASK_PRIO_NET_OTA           6   // > time while Uploading/Rebooting
#define TASK_PRIO_TIME_OTA          3   // < net during OTA

// ---------------------------------------------------------------------------
// Hardware wiring — target-conditional (合宙 CORE ESP32-C3 default, ESP32-S3
// DevKitC-1 + WROOM-1 N16R8 opt-in). Adjust these pins if your board differs.
// Target detection uses ARDUINO_ESP32S3_DEV: injected by the build (-D), so it
// is valid regardless of header include order (docs/esp32s3_devkitc1_hw.md).
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ESP32S3_DEV)

// ESP32-S3-DevKitC-1: UART0 debug = GPIO43/44 via onboard bridge (Serial
// default pins, no macros needed). Native USB on GPIO19/20 — keep free.
// GPIO0/3/45/46 are strapping pins: encoder B moved off C3's GPIO3.
#define PIN_GPS_RX           1   // ESP32 RX <- GP22 TXD
#define PIN_GPS_TX           0   // ESP32 TX -> GP22 RXD (output-only, BOOT strap safe; fallback GPIO18)
#define PIN_GPS_PPS          4   // 1PPS input (RTC-domain)
#define PIN_OLED_SDA         8
#define PIN_OLED_SCL        10
#define PIN_ENC_A            2
#define PIN_ENC_B            9   // C3 uses 3 — strapping on S3
#define PIN_ENC_SW           5
#define PIN_LED_D4          12   // D4 RUN / WiFi
#define PIN_LED_D5          13   // D5 GPS / PPS / NTP ready
#define PIN_LED_RGB         48   // S3 onboard SK6812-mini RGB (D6, 3V3): D4->R / D5->G
#define GPS_PPS_RMT_CH       7   // direct-IDF RMT RX channel (S3: 0..7; avoid HAL RGB ch0)
                                 // NOTE: field-measured @48 on this board; original
                                 // V1.1 schematic routes it to 38 (clone/older wiring)
#define TASK_TIME_CORE       1   // dual-core: task-time alone on core 1

#else

// UART0 (合宙 CORE / CH343): GPIO20 RX, GPIO21 TX — debug via Serial @ 115200
// DX-GP22 GNSS on UART1 (board UART1_RX=GPIO1, UART1_TX=GPIO0; 9600 8N1; 1PPS after fix)
#define PIN_GPS_RX           1   // ESP32 RX <- GP22 TXD  (UART1_RX)
#define PIN_GPS_TX           0   // ESP32 TX -> GP22 RXD  (UART1_TX)
#define PIN_GPS_PPS          4   // 1PPS input
#define PIN_OLED_SDA         8
#define PIN_OLED_SCL        10
#define PIN_ENC_A            2
#define PIN_ENC_B            3
#define PIN_ENC_SW           5
#define PIN_LED_D4          12   // D4 RUN / WiFi
#define PIN_LED_D5          13   // D5 GPS / PPS / NTP ready
#define GPS_PPS_RMT_CH       1   // direct-IDF RMT RX channel (C3: 0..1; avoid HAL RGB ch0)
#define TASK_TIME_CORE       0   // C3 single-core: everything on core 0

#endif

#define GPS_UART_BAUD     9600
#define GPS_UART_NUM         1
#define GPS_DEBUG            0   // 1 = 每秒向 UART0 打印定位/PPS（time 任务内，默认关）
#define GPS_DEBUG_NMEA       0   // 1 = 把 NMEA 原文转发到 UART0

// SH1107 / SSD1107 0.96" 64x128 OLED over I2C (pins above; native portrait, setRotation(1) → 128x64 UI)
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH          64
#define OLED_HEIGHT        128
#define OLED_ROTATION        1   // 1 = landscape UI on portrait panel

// KY-040 rotary encoder (pins above)
// Soft layer (task-ui): one detent ≈ 4 quadrature edges; leftover bounce is dropped.
#define ENC_DETENT_STEPS            4
#define ENC_IDLE_CLEAR_MS          80   // rest this long → clear sub-detent remainder
#define ENC_MIN_STEP_MS            50   // min gap between UI ticks (smooth, not bursty)
#define ENC_ISR_DEBOUNCE_US       250   // ISR edge floor; Gray table rejects 2-bit jumps

// On-board LEDs (合宙 CORE D4/D5 on C3; S3 merges both onto the onboard RGB @38)
// Active HIGH on C3. RGB brightness cap: WS2812-class @3V3 is very bright.
#define LED_RGB_BRIGHTNESS  12   // 0-255 per channel on S3 RGB

// SoftAP for web WiFi setup (SSID prefix). Password default: NTP-<MAC low 16-bit hex>.
#define AP_SSID_PREFIX      "NTP-Setup"
// Deprecated fixed password — B2 uses derivedSoftApPassword() / NVS `appw`.
#define AP_PASSWORD_LEGACY  "12345678"

// NTP
#define NTP_UDP_PORT         123
#define NTP_EPOCH_DELTA   2208988800UL  // 1900 -> 1970
// B1 rate limit / Kiss-o'-Death (task-time only; static client table)
#define NTP_CLIENT_SLOTS              12
#define NTP_RATE_PER_IP_PER_SEC        4
#define NTP_GLOBAL_RATE_PER_SEC       32
#define NTP_RATE_WINDOW_MS          1000
#define NTP_RATE_TO_DENY_MS        10000  // sustained over-limit → DENY cooldown
#define NTP_DENY_COOLDOWN_MS       60000
#define NTP_MAX_PACKETS_PER_LOOP       8
// B3 ACL AllowList (exact IPv4; Off by default)
#define NTP_ACL_MAX_ENTRIES            8
// B4: periodic UART0 summary from task-time (0 = off)
#define NTP_STATUS_LOG_MS          60000

// UI / timing
#define DISPLAY_REFRESH_MS   250
// OLED idle blanking: panel off (0xAE) after this much time without encoder
// input or an incoming toast; any encoder action wakes it (first action only
// wakes). Prevents 24/7 static-image burn-in on the home screen. Runtime
// setting NVS `ooff` (AppSettings.oledIdleOffMs); this is the factory default.
#define OLED_IDLE_OFF_DEFAULT_MS 600000  // 10 min; 0 = always on
// Factory reset: hold the encoder switch this long through power-on to wipe
// all settings back to defaults (checked in setup() before settings load).
#define FACTORY_RESET_HOLD_MS    3000
// Menu: FreeMono 9pt. 4 rows; bar shorter than pitch → gap.
#define OLED_MENU_TEXT_SIZE    1
#define OLED_MENU_ROWS         4
#define OLED_MENU_ROW_H       16
#define OLED_MENU_BAR_H       13
#define OLED_MENU_BASELINE    11  // GFX custom-font cursor is the baseline
#define OLED_MENU_Y0           0
#define OLED_UI_MARK        "FM2"
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_NO_AP_REBEGIN_MS    3000  // re-WiFi.begin while Connecting after reason 201
// Ignore local ASSOC_LEAVE (8) etc. right after our own disconnect()+begin().
#define WIFI_DISC_GRACE_MS       2500
#define WIFI_SCAN_TIMEOUT_MS    15000  // async scan must not hang reconnect give-up
// OLED keeps "Scanning..." across STA join + one radio scan; only then show fail.
#define WIFI_SCAN_UI_TIMEOUT_MS 40000
#define WIFI_SCAN_CACHE_MS       8000  // reuse lastScan_ instead of scanDelete()
#define IP_CONFLICT_TIMEOUT_MS   800
// Auto-reconnect: unlimited retries — signal loss must never become SoftAP
// provisioning (OLED "Web Setup" is the manual path). Backoff: immediate,
// 2s/5s/10s/30s, then steady 60s retry forever. Limits of 0 = unlimited.
#define WIFI_RECONNECT_MAX_ATTEMPTS      0
#define WIFI_RECONNECT_GIVEUP_MS         0
#define WIFI_RECONNECT_BACKOFF_0_MS      0
#define WIFI_RECONNECT_BACKOFF_1_MS   2000
#define WIFI_RECONNECT_BACKOFF_2_MS   5000
#define WIFI_RECONNECT_BACKOFF_3_MS  10000
#define WIFI_RECONNECT_BACKOFF_4_MS  30000
#define WIFI_RECONNECT_BACKOFF_5_MS  60000
// Boot join with saved credentials retries forever (linkLoss arms): SoftAP
// is manual-only (menu Web Setup) or for empty NVS — never an automatic
// escape. Runtime link loss likewise retries forever.
// Set to 0 to restore legacy give-up-and-SoftAP behavior.
#define WIFI_LINK_LOSS_RETRY_FOREVER 1
// Forever mode: "[wifi] still retrying" every N attempts (30s backoff each → N=20 ≈ 10min).
#define WIFI_LINK_LOSS_LOG_EVERY     20
#define GPS_NMEA_MAX_BYTES_PER_LOOP 256
// TinyGPSPlus isValid() stays true after last sentence; require fresh age + sats>0.
#define GPS_FIX_MAX_AGE_MS           5000

// Status LEDs: healthy "good" states use heartbeat (not solid) so hangs are visible
#define LED_HEARTBEAT_ON_MS         900
#define LED_HEARTBEAT_OFF_MS        100
#define LED_TASK_STALE_MS          3000
#define LED_PANIC_HALF_PERIOD_MS    100

// Local clock / GPS cross-check (see docs/local_clock_gps_check.md)
#define CLK_RESIDUAL_WARN_MS         50  // quality / UI warn floor (Degraded)
#define CLK_RESIDUAL_FAIL_MS        100  // |r| >= this → AnomalyPolicy
#define CLK_RESIDUAL_RELOCK_MS       30
#define CLK_RELOCK_COUNT              3
#define CLK_PPS_INTERVAL_MAX_ERR_US 5000  // outlier vs last accepted edge → drop edge
#define CLK_PPS_UNSTABLE_COUNT        3   // consecutive outliers → soft unsync
#define CLK_HOLDOVER_SHORT_SEC       30
#define CLK_HOLDOVER_LONG_SEC       300
#define CLK_PPS_EDGE_RING            16
#define CLK_PPM_EMA_ALPHA          0.2f
// Average this many 1s PPS intervals before EMA (needs span+1 edges).
#define CLK_PPM_SPAN_SEC              8
// Optional die-temp first-order ppm trim (NVS tcmp, default Off).
#define CLK_TEMP_COMP_DEFAULT         0
// Default 0 until calibrated: measured die↔XO coupling ≈ -0.10 ppm/°C
// (docs/ppm_monitor_20260916.md), NOT the old guess of -0.50. Wrong k in
// holdover adds error instead of removing it. Calibrate before enabling.
#define CLK_TEMP_COEFF_CENTI          (0)
// Bound the trim so a mis-calibrated k can never hurt more than ±2 ppm
// (≈0.6 ms per 300 s of holdover).
#define CLK_TEMP_CORR_MAX_PPM       2.0f
#define CLK_TEMP_SAMPLE_MS         1000
// Below this residual while Locked, keep PPS-only advance (no NMEA re-anchor).
#define CLK_LOCKED_SLEW_MS            5
// ISR→task PPS queue (missed edges under WiFi load).
#define GPS_PPS_ISR_QUEUE             8
// RMT RX hardware capture of the PPS edge (docs/s3_deep_dive_roadmap.md #1):
// PARKED (2026-09-18) — Arduino-ESP32 2.0.17 / IDF 4.4.7 legacy RMT RX on S3
// delivers only EMPTY ringbuf items (2 per edge, both the HAL rmtRead(cb)
// wrapper and a direct-IDF driver path with RMT_MEM_OWNER_RX claimed; raw
// channel status constant at 0x2a8150). Platform-level data-path defect,
// not fixable app-side. Reopen on Arduino 3.x / IDF 5 (new RMT driver).
#define GPS_PPS_RMT_EN                0   // 0 = legacy GPIO ISR only
#define GPS_PPS_RMT_QUEUE             8
#define GPS_PPS_RMT_TICK_NS        1000   // 1 µs symbols (80 MHz / 80)
#define GPS_PPS_RMT_WINDOW_MS        20   // capture window after the edge
#define GPS_PPS_RMT_FILTER_NS      1000   // hw-glitch filter: drop <1 µs pulses
#define GPS_PPS_RMT_HOLD_MS         700    // hold a GPIO edge for its refinement
#define GPS_PPS_RMT_STALE_MS        2100   // no RMT edges for this long -> fall back to GPIO
// Missed PPS seconds ≥ this → Unsynced (not silent catch-up only).
#define CLK_PPS_MISS_UNSYNC           3
// Holdover dispersion: floor crystal error (ppm) when EMA is still small.
// Cheap MCU XO + unknown temp: 50 ppm is more honest than 20 for free-run bound.
#define CLK_HOLDOVER_PPM_FLOOR       50.0f
// NTP-like PHI (ppm): never grow slower than this during holdover.
#define CLK_HOLDOVER_PHI_PPM         15.0f
// Extra uncertainty booked when entering / while in holdover (ms).
#define CLK_HOLDOVER_ENTRY_MS       100
// Safety exit when already-degraded residual + growth exceeds this. Primary
// holdover limit is still holdoverSec (30/300); at 50 ppm × 300 s growth is
// only ~15 ms, so this mainly catches high residual on entry.
#define CLK_HOLDOVER_MAX_QUALITY_MS 500
// Task panic (LED stale) for this long → soft restart.
#define LED_TASK_PANIC_RESTART_MS 15000
// Restart if free heap stays below this (fragmentation / leak).
#define HEAP_RESTART_BYTES         10240
#define HEAP_RESTART_SAMPLES            5

// Optional external RTC assist (docs/ext_clock_design.md). Default off until
// a DS3231 (or similar) is wired on the OLED I2C bus (addr 0x68).
#ifndef EXT_RTC_EN
#define EXT_RTC_EN                      0
#endif
#define EXT_RTC_I2C_ADDR             0x68
// Holdover dispersion floor when ExtClock is healthy (DS3231 TCXO-class ±2 ppm).
#define EXT_RTC_PPM_FLOOR             2.0f

