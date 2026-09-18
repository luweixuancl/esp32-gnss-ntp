#pragma once

// Boot-time OTA helpers (deferred rollback confirm + partition labels + busy gate).
// Implementation in src/ota_support.cpp.

#include <stddef.h>
#include <stdint.h>

void otaSetBusy(bool busy);
bool otaIsBusy();

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
