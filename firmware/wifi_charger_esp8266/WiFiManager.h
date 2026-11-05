#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "Types.h"
#include <ESP8266WiFi.h>

enum WifiMode {
  WIFI_MODE_SETUP_AP = 1,   // Mode 1: No router configured. Pure SoftAP on Ch1. Zero background scan.
  WIFI_MODE_NORMAL_STA = 2, // Mode 2: Connected to router. SoftAP off. Full LAN performance.
  WIFI_MODE_FALLBACK_AP = 3 // Mode 3: Router offline > 10s. SoftAP active. STA scanning paused if phone connected.
};

class WiFiManager {
public:
  WiFiManager();

  void begin();
  void loop(); // Call in loop() for reconnection state machine

  bool isConnected() const;
  String getSsid() const;
  IPAddress getStaIp() const;
  String getStaIpString() const;
  int getRssi() const;
  bool hasIpChanged();

  WifiMode getWifiMode() const { return _currentMode; }
  bool isPhoneConnectedToAp() const;
  bool isSetupMode() const { return _currentMode == WIFI_MODE_SETUP_AP; }

  void connectNew(const char* ssid, const char* pass);
  void resetCredentials();

private:
  WifiMode      _currentMode;
  bool          _lastConnected;
  IPAddress     _lastIp;
  bool          _ipChanged;
  unsigned long _lastAttemptTime;
  unsigned long _disconnectStartTime;
  unsigned long _backoffIntervalMs;
  uint8_t       _failedAttempts;
  int           _lastStatus;
};

extern WiFiManager wifiMgr;

#endif // WIFI_MANAGER_H
