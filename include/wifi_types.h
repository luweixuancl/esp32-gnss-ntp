#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <vector>

// Shared WiFi value types — kept out of wifi_manager.h / app_ipc.h so UI
// modules can depend on snapshots without pulling the full manager.

struct WifiNetwork {
  String ssid;
  int32_t rssi = 0;
  wifi_auth_mode_t enc = WIFI_AUTH_OPEN;
};

// Cross-task readable link view. Written only from task-net via
// WifiManager::refreshLinkSnapshot(); readers take a copy.
struct WifiLinkSnapshot {
  bool staUp = false;
  bool apUp = false;
  char staSsid[33] = {};
  char apSsid[33] = {};
  int32_t rssi = 0;
  IPAddress staIp;
  IPAddress apIp;
  IPAddress gateway;
};
