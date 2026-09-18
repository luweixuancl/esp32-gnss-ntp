#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "app_ipc.h"

// OTA module (task-net owns all Update / partition / priority mutations).
// Other tasks only observe gIpc.otaBusy / gIpc.otaPhase — never call write APIs.

class OtaService {
 public:
  void begin();

  // ---- queries (safe from any task; mirror of published IPC) ----
  bool isBusy() const { return ipcOtaBusy(); }
  OtaPhase phase() const { return ipcOtaPhase(); }
  const char* phaseLabel() const { return ipcOtaPhaseLabel(); }
  bool sessionActive() const { return sessionActive_; }
  bool succeeded() const { return success_; }

  uint16_t expectedChipId() const;
  const char* expectedChipName() const;
  const char* runningLabel() const;
  const char* nextLabel() const;
  const char* imageStateLabel() const;
  size_t nextSlotSize() const;

  // ---- task-net only (HTTP adapter / composition root) ----
  // contentLengthHint: 0 if unknown; used to reject oversized multipart early.
  bool beginSession(size_t contentLengthHint, char* err, size_t errLen);
  bool write(const uint8_t* data, size_t len, char* err, size_t errLen);
  bool finish(char* err, size_t errLen);  // validate end → Rebooting
  void fail(const char* reason);          // abort → Failed (LED linger)
  void clear();                           // force Idle

  // Feed task-net liveness while blocked in flash write / HTTP upload.
  void kickNetAlive();

  // Deferred rollback confirm + Failed→Idle timeout. Call from task-net loop.
  void poll();

 private:
  void publish(OtaPhase phase, bool busy);
  void applyOtaPriorities(bool enable);
  bool validatePrefix(const uint8_t* data, size_t len, char* err, size_t errLen) const;
  void setErr(char* err, size_t errLen, const char* msg) const;

  bool sessionActive_ = false;
  bool headerChecked_ = false;
  bool success_ = false;
  bool confirmDone_ = false;
  bool prioBoosted_ = false;
  uint32_t failUntilMs_ = 0;
  uint32_t lastLogBytes_ = 0;
  UBaseType_t savedPrioTime_ = 0;
  UBaseType_t savedPrioNet_ = 0;
};

extern OtaService gOta;
