#include "ota_support.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

void otaMarkAppValidIfNeeded() {
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

const char* otaRunningPartitionLabel() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return (p && p->label) ? p->label : "?";
}

const char* otaNextPartitionLabel() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return (p && p->label) ? p->label : "?";
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
