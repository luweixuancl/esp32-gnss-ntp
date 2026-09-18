#include "ota_service.h"

#include <Update.h>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <string.h>

#include "config.h"

OtaService gOta;

void OtaService::begin() {
  publish(OtaPhase::Idle, false);
}

void OtaService::setErr(char* err, size_t errLen, const char* msg) const {
  if (err == nullptr || errLen == 0) {
    return;
  }
  strncpy(err, msg ? msg : "", errLen - 1);
  err[errLen - 1] = '\0';
}

void OtaService::publish(OtaPhase phase, bool busy) {
  gIpc.otaPhase = phase;
  gIpc.otaBusy = busy;
}

void OtaService::applyOtaPriorities(bool enable) {
  // FreeRTOS: raise task-net above task-time so flash/WiFi upload is not
  // preempted by the 1 ms time loop (critical on single-core C3).
  if (enable) {
    if (prioBoosted_) {
      return;
    }
    if (gIpc.taskTime) {
      savedPrioTime_ = uxTaskPriorityGet(gIpc.taskTime);
      vTaskPrioritySet(gIpc.taskTime, TASK_PRIO_TIME_OTA);
    }
    if (gIpc.taskNet) {
      savedPrioNet_ = uxTaskPriorityGet(gIpc.taskNet);
      vTaskPrioritySet(gIpc.taskNet, TASK_PRIO_NET_OTA);
    }
    prioBoosted_ = true;
    Serial.printf("[ota] prio boost net=%u time=%u\n",
                  static_cast<unsigned>(TASK_PRIO_NET_OTA),
                  static_cast<unsigned>(TASK_PRIO_TIME_OTA));
  } else if (prioBoosted_) {
    if (gIpc.taskTime) {
      vTaskPrioritySet(gIpc.taskTime, savedPrioTime_);
    }
    if (gIpc.taskNet) {
      vTaskPrioritySet(gIpc.taskNet, savedPrioNet_);
    }
    prioBoosted_ = false;
    Serial.println("[ota] prio restored");
  }
}

void OtaService::kickNetAlive() {
  esp_task_wdt_reset();
  ipcKickNet();
}

void OtaService::clear() {
  if (Update.isRunning()) {
    Update.abort();
  }
  applyOtaPriorities(false);
  sessionActive_ = false;
  headerChecked_ = false;
  success_ = false;
  failUntilMs_ = 0;
  lastLogBytes_ = 0;
  hdrLen_ = 0;
  publish(OtaPhase::Idle, false);
}

void OtaService::fail(const char* reason) {
  if (sessionActive_ || Update.isRunning()) {
    Update.abort();
  }
  sessionActive_ = false;
  headerChecked_ = false;
  success_ = false;
  hdrLen_ = 0;
  applyOtaPriorities(false);
  failUntilMs_ = millis() + OTA_FAIL_LED_MS;
  publish(OtaPhase::Failed, false);
  Serial.printf("[ota] fail: %s\n", reason ? reason : "?");
}

bool OtaService::beginSession(size_t contentLengthHint, char* err, size_t errLen) {
  clear();
  if (Update.isRunning()) {
    setErr(err, errLen, "OTA already running");
    fail("OTA already running");
    return false;
  }
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (next == nullptr) {
    setErr(err, errLen, "No OTA partition");
    fail("No OTA partition");
    return false;
  }
  // Multipart Content-Length includes boundary overhead; allow a small slack.
  if (contentLengthHint > 0 && contentLengthHint > next->size + 4096) {
    setErr(err, errLen, "Upload too large for OTA slot");
    fail("Upload too large for OTA slot");
    return false;
  }
  Serial.printf("[ota] start → %s slot=%u chip=%s\n", next->label,
                static_cast<unsigned>(next->size), expectedChipName());
  if (!Update.begin(next->size)) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Update.begin failed: %s", Update.errorString());
    setErr(err, errLen, buf);
    fail(buf);
    return false;
  }
  sessionActive_ = true;
  headerChecked_ = false;
  success_ = false;
  lastLogBytes_ = 0;
  hdrLen_ = 0;
  applyOtaPriorities(true);
  publish(OtaPhase::Uploading, true);
  kickNetAlive();
  Serial.println("[ota] phase=uploading (NTP refused, work shed, prio boosted)");
  return true;
}

bool OtaService::validatePrefix(const uint8_t* data, size_t len, char* err, size_t errLen) const {
  if (data == nullptr || len < 14) {
    setErr(err, errLen, "Image header too short");
    return false;
  }
  if (data[0] != ESP_IMAGE_HEADER_MAGIC) {
    setErr(err, errLen, "Not an ESP app image (magic!=0xE9); use firmware.bin not merged");
    return false;
  }
  // segment_count at byte 1 — reject empty / absurd headers (HTML, truncated).
  if (data[1] < 1 || data[1] > 16) {
    setErr(err, errLen, "Bad image segment_count; re-download firmware.bin");
    return false;
  }
  const uint16_t chip = static_cast<uint16_t>(data[12]) |
                        (static_cast<uint16_t>(data[13]) << 8);
  const uint16_t expect = expectedChipId();
  if (chip != expect) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Wrong chip_id 0x%04x (need 0x%04x %s)", chip, expect,
             expectedChipName());
    setErr(err, errLen, buf);
    return false;
  }
  return true;
}

bool OtaService::write(const uint8_t* data, size_t len, char* err, size_t errLen) {
  if (!sessionActive_) {
    setErr(err, errLen, "No OTA session");
    return false;
  }
  if (data == nullptr || len == 0) {
    return true;
  }
  kickNetAlive();

  const uint8_t* out = data;
  size_t outLen = len;

  if (!headerChecked_) {
    // Accumulate until we can validate the ESP image header (WebServer may
    // deliver a tiny first WRITE in edge cases).
    while (hdrLen_ < sizeof(hdrBuf_) && outLen > 0) {
      hdrBuf_[hdrLen_++] = *out++;
      outLen--;
    }
    if (hdrLen_ < 14) {
      return true;  // wait for more bytes
    }
    if (!validatePrefix(hdrBuf_, hdrLen_, err, errLen)) {
      fail(err && err[0] ? err : "bad image header");
      return false;
    }
    headerChecked_ = true;
    if (Update.write(hdrBuf_, hdrLen_) != hdrLen_) {
      char buf[96];
      snprintf(buf, sizeof(buf), "Write failed: %s", Update.errorString());
      setErr(err, errLen, buf);
      fail(buf);
      return false;
    }
  }

  if (outLen > 0) {
    if (Update.write(const_cast<uint8_t*>(out), outLen) != outLen) {
      char buf[96];
      snprintf(buf, sizeof(buf), "Write failed: %s", Update.errorString());
      setErr(err, errLen, buf);
      fail(buf);
      return false;
    }
  }

  const size_t prog = Update.progress();
  if (prog >= lastLogBytes_ + OTA_PROGRESS_LOG_BYTES) {
    lastLogBytes_ = static_cast<uint32_t>(prog);
    Serial.printf("[ota] progress %u / %u\n", static_cast<unsigned>(prog),
                  static_cast<unsigned>(Update.size()));
    kickNetAlive();
  }
  return true;
}

bool OtaService::finish(char* err, size_t errLen) {
  if (!sessionActive_) {
    setErr(err, errLen, "Upload aborted");
    fail("Upload aborted");
    return false;
  }
  kickNetAlive();
  if (!headerChecked_) {
    setErr(err, errLen, "Missing image header");
    fail("Missing image header");
    return false;
  }
  const size_t written = Update.progress();
  if (written < OTA_MIN_IMAGE_BYTES) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Image too small (%u B) — truncated download?",
             static_cast<unsigned>(written));
    setErr(err, errLen, buf);
    fail(buf);
    return false;
  }
  if (!Update.end(true)) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Update.end failed: %s", Update.errorString());
    setErr(err, errLen, buf);
    fail(buf);
    return false;
  }

  // Update.end only checks magic; verify full image checksum before reboot.
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (boot != nullptr) {
    esp_image_metadata_t meta{};
    const esp_partition_pos_t pos = {.offset = boot->address, .size = boot->size};
    const esp_err_t v = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta);
    if (v != ESP_OK) {
      if (running != nullptr) {
        esp_ota_set_boot_partition(running);
      }
      char buf[96];
      snprintf(buf, sizeof(buf), "Image verify failed (%s) — re-download bin",
               esp_err_to_name(v));
      setErr(err, errLen, buf);
      fail(buf);
      return false;
    }
    Serial.printf("[ota] image verify ok bytes=%u\n", static_cast<unsigned>(meta.image_len));
  }

  sessionActive_ = false;
  success_ = true;
  // Keep busy + boosted prio until ESP.restart() in the HTTP done handler.
  publish(OtaPhase::Rebooting, true);
  Serial.println("[ota] phase=rebooting");
  return true;
}

void OtaService::poll() {
  // Failed LED linger → Idle
  if (gIpc.otaPhase == OtaPhase::Failed && failUntilMs_ != 0 &&
      static_cast<int32_t>(millis() - failUntilMs_) >= 0) {
    failUntilMs_ = 0;
    publish(OtaPhase::Idle, false);
  }

  if (confirmDone_ || gIpc.otaBusy) {
    return;
  }
  if (millis() < OTA_MARK_VALID_AFTER_MS) {
    return;
  }
  if (gIpc.kickTimeMs == 0 || gIpc.kickNetMs == 0 || gIpc.kickUiMs == 0) {
    return;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    confirmDone_ = true;
    return;
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t err = esp_ota_get_state_partition(running, &state);
  if (err != ESP_OK) {
    Serial.printf("[ota] get_state skipped (%s)\n", esp_err_to_name(err));
    confirmDone_ = true;
    return;
  }
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t m = esp_ota_mark_app_valid_cancel_rollback();
    if (m == ESP_OK) {
      Serial.printf("[ota] app valid (cancel rollback) label=%s\n", running->label);
    } else {
      Serial.printf("[ota] mark valid failed: %s\n", esp_err_to_name(m));
    }
  } else {
    Serial.printf("[ota] running=%s state=%d\n", running->label, static_cast<int>(state));
  }
  confirmDone_ = true;
}

uint16_t OtaService::expectedChipId() const {
#if defined(ARDUINO_ESP32S3_DEV)
  return static_cast<uint16_t>(ESP_CHIP_ID_ESP32S3);
#else
  return static_cast<uint16_t>(ESP_CHIP_ID_ESP32C3);
#endif
}

const char* OtaService::expectedChipName() const {
#if defined(ARDUINO_ESP32S3_DEV)
  return "ESP32-S3";
#else
  return "ESP32-C3";
#endif
}

const char* OtaService::runningLabel() const {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return (p && p->label) ? p->label : "?";
}

const char* OtaService::nextLabel() const {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return (p && p->label) ? p->label : "?";
}

size_t OtaService::nextSlotSize() const {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

const char* OtaService::imageStateLabel() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return "none";
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return "n/a";
  }
  switch (state) {
    case ESP_OTA_IMG_NEW:
      return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
      return "pending";
    case ESP_OTA_IMG_VALID:
      return "valid";
    case ESP_OTA_IMG_INVALID:
      return "invalid";
    case ESP_OTA_IMG_ABORTED:
      return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
      return "undefined";
  }
}
