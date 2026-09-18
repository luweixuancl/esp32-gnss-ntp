#pragma once

// OTA helpers: busy gate (shed NTP/work), UI phase for LEDs, deferred rollback
// confirm, image header validation. Implementation in src/ota_support.cpp.

#include <stddef.h>
#include <stdint.h>

// LED / status UI phase (orthogonal to busy: Failed clears busy but keeps LED).
enum class OtaUiPhase : uint8_t {
  Idle = 0,
  Uploading = 1,  // flash write in progress — NTP refused, work shed
  Rebooting = 2,  // image accepted — solid success LED until restart
  Failed = 3,     // brief error LED, then auto-clears to Idle
};

void otaEnterUploading();
void otaEnterRebooting();
void otaEnterFailed();
void otaClear();  // force Idle (tests / abort edge)

bool otaIsBusy();  // Uploading or Rebooting — block NTP & shed background work
OtaUiPhase otaUiPhase();
const char* otaUiPhaseLabel();

// Feed task-net TWDT + ipcKickNet so LED panic / TWDT cannot fire mid-upload.
void otaKickWatchdogs();

// Deferred confirm: call from task-net after tasks have been alive long enough.
void otaPollConfirmValid();

// Validate ESP app image header (magic + chip_id) against this build target.
// Returns false and fills err (may be null) on mismatch. Needs >= 14 bytes.
bool otaValidateImagePrefix(const uint8_t* data, size_t len, char* err, size_t errLen);

uint16_t otaExpectedChipId();
const char* otaExpectedChipName();

const char* otaRunningPartitionLabel();
const char* otaNextPartitionLabel();
const char* otaImageStateLabel();
size_t otaNextPartitionSize();
