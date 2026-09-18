#include "ota_support.h"

#include <Arduino.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <string.h>

#include "app_ipc.h"
#include "config.h"

namespace {

volatile bool gOtaBusy = false;
bool gOtaConfirmDone = false;

void markValidNow() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return;
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t err = esp_ota_get_state_partition(running, &state);
  if (err != ESP_OK) {
    // Older bootloader / no otadata — nothing to confirm.
    Serial.printf("[ota] get_state skipped (%s)\n", esp_err_to_name(err));
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
}

}  // namespace

void otaSetBusy(bool busy) {
  gOtaBusy = busy;
  if (busy) {
    otaKickWatchdogs();
  }
}

bool otaIsBusy() {
  return gOtaBusy;
}

void otaKickWatchdogs() {
  esp_task_wdt_reset();
  ipcKickNet();
}

void otaPollConfirmValid() {
  if (gOtaConfirmDone || gOtaBusy) {
    return;
  }
  if (millis() < OTA_MARK_VALID_AFTER_MS) {
    return;
  }
  // Require all three tasks to have kicked at least once (system actually running).
  if (gIpc.kickTimeMs == 0 || gIpc.kickNetMs == 0 || gIpc.kickUiMs == 0) {
    return;
  }
  markValidNow();
  gOtaConfirmDone = true;
}

uint16_t otaExpectedChipId() {
#if defined(ARDUINO_ESP32S3_DEV)
  return static_cast<uint16_t>(ESP_CHIP_ID_ESP32S3);
#else
  return static_cast<uint16_t>(ESP_CHIP_ID_ESP32C3);
#endif
}

const char* otaExpectedChipName() {
#if defined(ARDUINO_ESP32S3_DEV)
  return "ESP32-S3";
#else
  return "ESP32-C3";
#endif
}

bool otaValidateImagePrefix(const uint8_t* data, size_t len, char* err, size_t errLen) {
  auto setErr = [&](const char* msg) {
    if (err != nullptr && errLen > 0) {
      strncpy(err, msg, errLen - 1);
      err[errLen - 1] = '\0';
    }
  };
  if (data == nullptr || len < 14) {
    setErr("Image header too short");
    return false;
  }
  if (data[0] != ESP_IMAGE_HEADER_MAGIC) {
    setErr("Not an ESP app image (magic!=0xE9); use firmware.bin not merged");
    return false;
  }
  const uint16_t chip = static_cast<uint16_t>(data[12]) |
                        (static_cast<uint16_t>(data[13]) << 8);
  const uint16_t expect = otaExpectedChipId();
  if (chip != expect) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Wrong chip_id 0x%04x (need 0x%04x %s)", chip, expect,
             otaExpectedChipName());
    setErr(buf);
    return false;
  }
  return true;
}

const char* otaRunningPartitionLabel() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return (p && p->label) ? p->label : "?";
}

const char* otaNextPartitionLabel() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return (p && p->label) ? p->label : "?";
}

size_t otaNextPartitionSize() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

const char* otaImageStateLabel() {
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
