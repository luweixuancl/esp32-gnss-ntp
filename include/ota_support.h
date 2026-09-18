#pragma once

// Boot-time OTA helpers (rollback confirm + partition labels for /status).
// Implementation in src/ota_support.cpp.

void otaMarkAppValidIfNeeded();
const char* otaRunningPartitionLabel();
const char* otaNextPartitionLabel();
const char* otaImageStateLabel();
