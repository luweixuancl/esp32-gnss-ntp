#include "app_ipc.h"
#include <cstring>

AppIpc gIpc;

bool ipcInit() {
  gIpc.netReq = xQueueCreate(8, sizeof(NetRequest));
  gIpc.uiMsg = xQueueCreate(8, sizeof(UiMsg));
  gIpc.settingsMutex = xSemaphoreCreateMutex();
  gIpc.scanMutex = xSemaphoreCreateMutex();
  return gIpc.netReq && gIpc.uiMsg && gIpc.settingsMutex && gIpc.scanMutex;
}

bool settingsLock(TickType_t ticks) {
  return xSemaphoreTake(gIpc.settingsMutex, ticks) == pdTRUE;
}

void settingsUnlock() {
  xSemaphoreGive(gIpc.settingsMutex);
}

bool settingsCopy(TickType_t wait, AppSettings* out) {
  if (out == nullptr) {
    return false;
  }
  if (!settingsLock(wait)) {
    return false;
  }
  *out = gSettings;
  settingsUnlock();
  return true;
}

bool settingsCommit(TickType_t wait, const AppSettings& in) {
  if (!settingsLock(wait)) {
    return false;
  }
  gSettings = in;
  settingsUnlock();
  gStore.save(in);
  return true;
}

bool postNetRequest(const NetRequest& req) {
  if (!gIpc.netReq) {
    return false;
  }
  return xQueueSend(gIpc.netReq, &req, 0) == pdTRUE;
}

bool postUiText(const char* text) {
  if (!gIpc.uiMsg) {
    return false;
  }
  UiMsg msg{};
  msg.type = UiMsgType::Text;
  strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
  msg.text[sizeof(msg.text) - 1] = '\0';
  return xQueueSend(gIpc.uiMsg, &msg, 0) == pdTRUE;
}

const char* ipcOtaPhaseLabel() {
  switch (gIpc.otaPhase) {
    case OtaPhase::Uploading:
      return "uploading";
    case OtaPhase::Rebooting:
      return "rebooting";
    case OtaPhase::Failed:
      return "failed";
    case OtaPhase::Idle:
    default:
      return "idle";
  }
}
