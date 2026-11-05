#include "WiFiManager.h"
#include "ConfigManager.h"
#include "APManager.h"
#include "DnsPortal.h"
#include "MdnsManager.h"
#include "DiscoveryManager.h"
#include "HttpServer.h"

WiFiManager wifiMgr;

WiFiManager::WiFiManager()
  : _currentMode(WIFI_MODE_SETUP_AP),
    _lastConnected(false),
    _lastIp(0, 0, 0, 0),
    _ipChanged(false),
    _lastAttemptTime(0),
    _disconnectStartTime(0),
    _backoffIntervalMs(15000),
    _failedAttempts(0),
    _lastStatus(-1)
{
}

bool WiFiManager::isPhoneConnectedToAp() const {
  return apMgr.isActive() && (WiFi.softAPgetStationNum() > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// begin() - Called once from setup()
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // Never power-down radio
  WiFi.setOutputPower(20.5f);

  const DeviceConfig& cfg = configMgr.get();
  bool hasWifi = (strlen(cfg.wifiSsid) > 0);

  if (hasWifi) {
    // ── Mode 2/3: Credentials configured. Attempt STA connection ────────────
    Serial.printf("[WiFiMgr] STA: Connecting to '%s'...\n", cfg.wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.config(0U, 0U, 0U); // Fresh DHCP

    // Parse saved BSSID string → uint8_t[6] for fast directed probe (<1.5s)
    uint8_t bssid[6] = {0};
    bool hasBssid = false;
    if (strlen(cfg.wifiBssid) == 17) {
      int b0,b1,b2,b3,b4,b5;
      if (sscanf(cfg.wifiBssid, "%x:%x:%x:%x:%x:%x", &b0,&b1,&b2,&b3,&b4,&b5) == 6) {
        bssid[0]=(uint8_t)b0; bssid[1]=(uint8_t)b1; bssid[2]=(uint8_t)b2;
        bssid[3]=(uint8_t)b3; bssid[4]=(uint8_t)b4; bssid[5]=(uint8_t)b5;
        hasBssid = (b0 || b1 || b2 || b3 || b4 || b5);
      }
    }

    if (hasBssid && cfg.wifiChannel >= 1 && cfg.wifiChannel <= 13) {
      Serial.printf("[WiFiMgr] BSSID-directed probe ch%d BSSID=%s...\n",
                    cfg.wifiChannel, cfg.wifiBssid);
      WiFi.begin(cfg.wifiSsid, cfg.wifiPass, cfg.wifiChannel, bssid, true);
    } else if (cfg.wifiChannel >= 1 && cfg.wifiChannel <= 13) {
      Serial.printf("[WiFiMgr] Channel-directed probe ch%d for '%s'...\n",
                    cfg.wifiChannel, cfg.wifiSsid);
      WiFi.begin(cfg.wifiSsid, cfg.wifiPass, cfg.wifiChannel);
    } else {
      WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    }

    // Wait up to 18 seconds for boot connect (allow full WPA2 4-way handshake & DHCP lease)
    unsigned long start = millis();
    unsigned long lastDot = 0;
    Serial.printf("[WiFiMgr] Connecting to '%s' (timeout: 18s)...", cfg.wifiSsid);
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 18000)) {
      delay(100);
      ESP.wdtFeed();
      yield();
      if (millis() - lastDot >= 1000) {
        lastDot = millis();
        Serial.print('.');
      }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
      // Successfully connected -> Mode 2 (Normal STA)
      _currentMode = WIFI_MODE_NORMAL_STA;
      _lastConnected = true;
      _lastIp = WiFi.localIP();
      _failedAttempts = 0;
      _disconnectStartTime = 0;

      int connectedCh = WiFi.channel();
      DeviceConfig& mcfg = configMgr.get();
      bool cfgChanged = false;

      if (connectedCh > 0 && connectedCh != mcfg.wifiChannel) {
        mcfg.wifiChannel = connectedCh;
        cfgChanged = true;
      }
      uint8_t* bssidBytes = WiFi.BSSID();
      if (bssidBytes) {
        char bssidStr[18];
        snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bssidBytes[0], bssidBytes[1], bssidBytes[2],
                 bssidBytes[3], bssidBytes[4], bssidBytes[5]);
        if (strcmp(mcfg.wifiBssid, bssidStr) != 0) {
          strncpy(mcfg.wifiBssid, bssidStr, sizeof(mcfg.wifiBssid) - 1);
          cfgChanged = true;
        }
      }
      if (cfgChanged) {
        configMgr.save();
      }

      // Stop SoftAP for zero latency LAN operation
      apMgr.stop();
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      Serial.printf("[WiFiMgr] CONNECTED (Mode 2: Normal STA)! SSID=%s IP=%s Ch=%d\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), connectedCh);
    } else {
      // Boot connect timed out -> Mode 3 (Fallback AP)
      _currentMode = WIFI_MODE_FALLBACK_AP;
      Serial.println(F("[WiFiMgr] Boot connect timed out. Transitioning to Mode 3 (Fallback AP)..."));
      _lastAttemptTime = millis();
      _disconnectStartTime = millis();
      _failedAttempts = 0;
      _lastConnected = false;
      apMgr.begin(1);
    }

  } else {
    // ── Mode 1: Pure Setup Mode (No credentials) ─────────────────────────
    _currentMode = WIFI_MODE_SETUP_AP;
    Serial.println(F("[WiFiMgr] Mode 1: Pure Setup Mode. Starting Setup SoftAP on Channel 1..."));
    _lastConnected = false;
    _disconnectStartTime = 0;
    apMgr.begin(1);
    httpSrvr.triggerAsyncScan();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() - Called every iteration from main loop()
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::loop() {
  const DeviceConfig& cfg = configMgr.get();
  bool hasCredentials = (strlen(cfg.wifiSsid) > 0);
  unsigned long now = millis();

  // ═════════════════════════════════════════════════════════════════════════
  // MODE 1: SETUP AP (No Wi-Fi configured)
  // ═════════════════════════════════════════════════════════════════════════
  if (!hasCredentials) {
    _currentMode = WIFI_MODE_SETUP_AP;
    if (!apMgr.isActive()) {
      Serial.println(F("[WiFiMgr] Setup Mode: Ensuring Setup SoftAP is active on Channel 1..."));
      apMgr.begin(1);
    }
    // CRITICAL: Zero background scans, zero reconnect attempts, zero channel hops!
    return;
  }

  // If credentials exist, check current status
  wl_status_t currentStatus = WiFi.status();

  // Diagnostic logging on state transition
  if ((int)currentStatus != _lastStatus) {
    _lastStatus = (int)currentStatus;
    switch (currentStatus) {
      case WL_IDLE_STATUS:
        Serial.println(F("[WiFiMgr] Status: IDLE"));
        break;
      case WL_NO_SSID_AVAIL:
        Serial.printf("[WiFiMgr] Status: SSID '%s' NOT FOUND\n", cfg.wifiSsid);
        break;
      case WL_CONNECTED:
        Serial.printf("[WiFiMgr] Status: CONNECTED! IP=%s GW=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str());
        break;
      case WL_CONNECT_FAILED:
        Serial.printf("[WiFiMgr] Status: CONNECT_FAILED for '%s'\n", cfg.wifiSsid);
        break;
      case WL_CONNECTION_LOST:
        Serial.println(F("[WiFiMgr] Status: CONNECTION_LOST"));
        break;
      case WL_DISCONNECTED:
        Serial.println(F("[WiFiMgr] Status: DISCONNECTED"));
        break;
      default:
        Serial.printf("[WiFiMgr] Status code: %d\n", (int)currentStatus);
        break;
    }
  }

  // ═════════════════════════════════════════════════════════════════════════
  // CONNECTED: Transition to / Stay in MODE 2 (Normal STA)
  // ═════════════════════════════════════════════════════════════════════════
  if (currentStatus == WL_CONNECTED && WiFi.localIP()[0] != 0) {
    _currentMode = WIFI_MODE_NORMAL_STA;
    _disconnectStartTime = 0;

    // Turn OFF SoftAP completely for maximum LAN bandwidth and 0 latency
    if (apMgr.isActive()) {
      apMgr.stop();
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      Serial.println(F("[WiFiMgr] Router connected! SoftAP stopped. Radio 100% STA."));
    }

    if (!_lastConnected) {
      _lastConnected = true;
      _lastIp = WiFi.localIP();
      _ipChanged = true;
      _failedAttempts = 0;
      _backoffIntervalMs = 15000;

      int connectedCh = WiFi.channel();
      DeviceConfig& mcfg = configMgr.get();
      bool cfgChanged = false;

      if (mcfg.wifiChannel != connectedCh && connectedCh > 0) {
        mcfg.wifiChannel = connectedCh;
        cfgChanged = true;
      }

      IPAddress curIp  = WiFi.localIP();
      IPAddress curGw  = WiFi.gatewayIP();
      IPAddress curSub = WiFi.subnetMask();
      IPAddress curDns = WiFi.dnsIP();
      if ((uint32_t)curIp != mcfg.cachedIp || (uint32_t)curGw != mcfg.cachedGateway) {
        mcfg.cachedIp      = (uint32_t)curIp;
        mcfg.cachedGateway = (uint32_t)curGw;
        mcfg.cachedSubnet  = (uint32_t)curSub;
        mcfg.cachedDns     = (uint32_t)curDns;
        cfgChanged = true;
      }

      uint8_t* bb = WiFi.BSSID();
      if (bb) {
        char bStr[18];
        snprintf(bStr, sizeof(bStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]);
        if (strcmp(mcfg.wifiBssid, bStr) != 0) {
          strncpy(mcfg.wifiBssid, bStr, sizeof(mcfg.wifiBssid) - 1);
          cfgChanged = true;
        }
      }

      if (cfgChanged) {
        configMgr.save();
      }

      Serial.printf("[WiFiMgr] MODE 2 ACTIVE: SSID=%s IP=%s Ch=%d BSSID=%s RSSI=%d dBm\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    connectedCh, mcfg.wifiBssid, WiFi.RSSI());

      mdnsMgr.restart();
      discoveryMgr.restart();

    } else if (WiFi.localIP() != _lastIp) {
      _lastIp = WiFi.localIP();
      _ipChanged = true;
      Serial.printf("[WiFiMgr] DHCP IP changed: %s\n", _lastIp.toString().c_str());
      mdnsMgr.restart();
      discoveryMgr.restart();
    }

  } else {
    // ═════════════════════════════════════════════════════════════════════════
    // DISCONNECTED: Handle 10-Second Grace Period & Mode 3 (Fallback AP)
    // ═════════════════════════════════════════════════════════════════════════
    if (_lastConnected) {
      _lastConnected = false;
      _disconnectStartTime = now;
      _lastAttemptTime = now;
      _failedAttempts = 0;
      WiFi.setAutoReconnect(false);
      Serial.println(F("[WiFiMgr] Router connection lost. Starting 10-second fast reconnect grace..."));
    }

    // After 10s disconnected: Launch Mode 3 (Fallback AP)
    if (_disconnectStartTime > 0 && (now - _disconnectStartTime >= 10000UL)) {
      if (!apMgr.isActive()) {
        _currentMode = WIFI_MODE_FALLBACK_AP;
        Serial.println(F("[WiFiMgr] Disconnected >10s → MODE 3: Launching Fallback SoftAP (192.168.4.1)..."));
        apMgr.begin(1);
      }
    }

    // ── CRITICAL: PHONE CONNECTED GUARD ─────────────────────────────────────
    // While a phone or PC is connected to the SoftAP:
    // COMPLETELY DISABLE / PAUSE ALL STA background scanning and channel hopping!
    // This guarantees the phone will NEVER get disconnected while user is using it.
    if (apMgr.isActive() && WiFi.softAPgetStationNum() > 0) {
      _lastAttemptTime = now; // Defer reconnect while client is on the portal
      return;
    }

    // ── Periodic Background Reconnect (Only when ZERO clients are on SoftAP) ──
    if (now - _lastAttemptTime >= 20000UL) {
      _lastAttemptTime = now;
      _failedAttempts++;
      WiFi.config(0U, 0U, 0U); // Fresh DHCP

      uint8_t bssid[6] = {0};
      bool hasBssid = false;
      if (strlen(cfg.wifiBssid) == 17) {
        int b0,b1,b2,b3,b4,b5;
        if (sscanf(cfg.wifiBssid, "%x:%x:%x:%x:%x:%x",&b0,&b1,&b2,&b3,&b4,&b5)==6) {
          bssid[0]=(uint8_t)b0; bssid[1]=(uint8_t)b1; bssid[2]=(uint8_t)b2;
          bssid[3]=(uint8_t)b3; bssid[4]=(uint8_t)b4; bssid[5]=(uint8_t)b5;
          hasBssid = (b0||b1||b2||b3||b4||b5);
        }
      }

      if (hasBssid && cfg.wifiChannel >= 1 && cfg.wifiChannel <= 13 && (_failedAttempts % 2 == 1)) {
        Serial.printf("[WiFiMgr] Background BSSID probe ch%d BSSID=%s '%s' (attempt #%d)...\n",
                      cfg.wifiChannel, cfg.wifiBssid, cfg.wifiSsid, _failedAttempts);
        WiFi.begin(cfg.wifiSsid, cfg.wifiPass, cfg.wifiChannel, bssid, true);
      } else if (cfg.wifiChannel >= 1 && cfg.wifiChannel <= 13 && (_failedAttempts % 2 == 1)) {
        Serial.printf("[WiFiMgr] Background channel probe ch%d '%s' (attempt #%d)...\n",
                      cfg.wifiChannel, cfg.wifiSsid, _failedAttempts);
        WiFi.begin(cfg.wifiSsid, cfg.wifiPass, cfg.wifiChannel);
      } else {
        Serial.printf("[WiFiMgr] Background full scan for '%s' (attempt #%d)...\n",
                      cfg.wifiSsid, _failedAttempts);
        WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
      }
    }
  }
}

bool WiFiManager::isConnected() const {
  return (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
}

String WiFiManager::getSsid() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.SSID();
  }
  const DeviceConfig& cfg = configMgr.get();
  if (strlen(cfg.wifiSsid) > 0) {
    return String(cfg.wifiSsid) + " (Connecting...)";
  }
  return String("None (SoftAP Only)");
}

IPAddress WiFiManager::getStaIp() const {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
}

String WiFiManager::getStaIpString() const {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
}

int WiFiManager::getRssi() const {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

bool WiFiManager::hasIpChanged() {
  if (_ipChanged) {
    _ipChanged = false;
    return true;
  }
  return false;
}

void WiFiManager::connectNew(const char* ssid, const char* pass) {
  if (!ssid || strlen(ssid) == 0) {
    Serial.println(F("[WiFiMgr] connectNew: Error - Empty SSID rejected."));
    return;
  }
  DeviceConfig& cfg = configMgr.get();
  strncpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid) - 1);
  cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = '\0';
  if (pass) {
    strncpy(cfg.wifiPass, pass, sizeof(cfg.wifiPass) - 1);
    cfg.wifiPass[sizeof(cfg.wifiPass) - 1] = '\0';
  } else {
    memset(cfg.wifiPass, 0, sizeof(cfg.wifiPass));
  }
  cfg.wifiChannel = 0;    // Reset so next boot does full scan first
  memset(cfg.wifiBssid, 0, sizeof(cfg.wifiBssid)); // Reset BSSID cache too
  configMgr.save();

  Serial.printf("[WiFiMgr] Saved new credentials for '%s'. Rebooting into Mode 2...\n", ssid);
  delay(300);
  ESP.restart();
}

void WiFiManager::resetCredentials() {
  DeviceConfig& cfg = configMgr.get();
  strncpy(cfg.wifiSsid, DEFAULT_WIFI_SSID, sizeof(cfg.wifiSsid) - 1);
  strncpy(cfg.wifiPass, DEFAULT_WIFI_PASS, sizeof(cfg.wifiPass) - 1);
  memset(cfg.wifiBssid, 0, sizeof(cfg.wifiBssid));
  cfg.wifiChannel = 0;
  WiFi.persistent(false);
  WiFi.disconnect(true);
  configMgr.save();

  Serial.println(F("[WiFiMgr] Wi-Fi credentials reset to default ('NO NET'). Rebooting..."));
  delay(300);
  ESP.restart();
}
