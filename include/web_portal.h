#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "settings.h"
#include "wifi_manager.h"

class GpsService;
class NtpServer;

class WebPortal {
 public:
  void begin(WifiManager* wifi, GpsService* gps, NtpServer* ntp);
  void loop();
  bool consumeConnectRequest(String& ssid, String& pass);

 private:
  void handleRoot();
  void handleSetupEntry();
  void handleSetup();
  void handleLogin();
  void handleLogout();
  void handleScan();
  void handleSave();
  void handleOtaDone();
  void handleOtaUpload();
  void handleStatus();
  void handleMetrics();
  bool sessionCookieOk();
  bool requireSession(bool htmlLogin);
  void sendLoginPage(const char* err);
  void issueSession();
  String writePassword() const;
  String buildPage(const String& title, const String& body, bool refresh = false) const;
  void sendNoCache();

  WebServer server_{80};
  WifiManager* wifi_ = nullptr;
  GpsService* gps_ = nullptr;
  NtpServer* ntp_ = nullptr;
  bool pendingConnect_ = false;
  bool started_ = false;
  String pendingSsid_;
  String pendingPass_;
  String sessionToken_;
  uint32_t sessionUntilMs_ = 0;
  // OTA upload state (single in-flight multipart; task-net only).
  bool otaAuthOk_ = false;
  bool otaStarted_ = false;
  bool otaSuccess_ = false;
  bool otaHeaderChecked_ = false;
  uint32_t otaLastLogBytes_ = 0;
  char otaError_[96] = {};
};
