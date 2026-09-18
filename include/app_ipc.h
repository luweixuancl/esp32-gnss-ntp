#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "settings.h"
#include "wifi_types.h"

// Cross-task IPC for RTOS refactor (time / net / ui).
// Modules own their logic; AppIpc carries shared snapshots and queues only.
// WiFi types live in wifi_types.h so UI code need not include WifiManager.

enum class NetReqType : uint8_t {
  ConnectWifi,
  ScanWifi,
  ApplyStaticIp,
  UseDhcp,
  StartWebSetup,
};

struct NetRequest {
  NetReqType type = NetReqType::ConnectWifi;
  char ssid[33] = {};
  char pass[65] = {};
  IPAddress staticIp;
  uint32_t seq = 0;  // OLED scan generation; 0 = unspecified
};

enum class UiMsgType : uint8_t {
  Text,
  ScanResult,
  ScanFailed,
};

struct UiMsg {
  UiMsgType type = UiMsgType::Text;
  char text[48] = {};
  uint32_t seq = 0;  // must match DisplayUi scanSeq_ for ScanResult/ScanFailed
  // Scan results: payload lives in shared scan buffer guarded by scanMutex.
};

// Published by OtaService (task-net). Readers (task-time / task-ui / LEDs)
// must not call into OtaService — only observe these fields.
enum class OtaPhase : uint8_t {
  Idle = 0,
  Uploading = 1,
  Rebooting = 2,
  Failed = 3,
};

struct AppIpc {
  QueueHandle_t netReq = nullptr;
  QueueHandle_t uiMsg = nullptr;
  SemaphoreHandle_t settingsMutex = nullptr;
  SemaphoreHandle_t scanMutex = nullptr;
  std::vector<WifiNetwork> scanResults;
  bool scanReady = false;
  bool setupAp = false;

  // Task liveness stamps (millis); StatusLeds panics if any go stale.
  volatile uint32_t kickTimeMs = 0;
  volatile uint32_t kickNetMs = 0;
  volatile uint32_t kickUiMs = 0;

  // OTA snapshot (written only by OtaService).
  volatile bool otaBusy = false;       // Uploading | Rebooting
  volatile OtaPhase otaPhase = OtaPhase::Idle;

  // Task handles for priority shed during OTA (set once from setup).
  TaskHandle_t taskTime = nullptr;
  TaskHandle_t taskNet = nullptr;
  TaskHandle_t taskUi = nullptr;
};

extern AppIpc gIpc;
extern AppSettings gSettings;
extern SettingsStore gStore;

bool ipcInit();
bool settingsLock(TickType_t ticks);
void settingsUnlock();
bool postNetRequest(const NetRequest& req);
bool postUiText(const char* text);

// Single write path for AppSettings (UI / Web / net must not call gStore.save
// directly). Copy under mutex; commit assigns gSettings then persists NVS.
bool settingsCopy(TickType_t wait, AppSettings* out);
bool settingsCommit(TickType_t wait, const AppSettings& in);

inline void ipcKickTime() { gIpc.kickTimeMs = millis(); }
inline void ipcKickNet() { gIpc.kickNetMs = millis(); }
inline void ipcKickUi() { gIpc.kickUiMs = millis(); }

inline bool ipcOtaBusy() { return gIpc.otaBusy; }
inline OtaPhase ipcOtaPhase() { return gIpc.otaPhase; }
const char* ipcOtaPhaseLabel();
