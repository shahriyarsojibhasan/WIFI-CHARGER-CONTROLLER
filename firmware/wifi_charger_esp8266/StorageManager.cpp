#include "StorageManager.h"

StorageManager storageMgr;

StorageManager::StorageManager() : _mounted(false), _lastInfoUpdateMs(0) {
  memset(&_cachedInfo, 0, sizeof(_cachedInfo));
}

void StorageManager::updateFsInfo() const {
  if (!_mounted) return;
  uint32_t now = millis();
  if (_lastInfoUpdateMs == 0 || (now - _lastInfoUpdateMs >= 30000)) {
    if (LittleFS.info(_cachedInfo)) {
      _lastInfoUpdateMs = now;
    }
  }
}

bool StorageManager::begin() {
  if (LittleFS.begin()) {
    _mounted = true;
    FSInfo fsInfo;
    if (LittleFS.info(fsInfo)) {
      Serial.printf("[StorageMgr] LittleFS mounted successfully. Total: %u KB, Used: %u KB (%.1f%%)\n",
                    (unsigned int)(fsInfo.totalBytes / 1024),
                    (unsigned int)(fsInfo.usedBytes / 1024),
                    fsInfo.totalBytes > 0 ? ((float)fsInfo.usedBytes / (float)fsInfo.totalBytes * 100.0f) : 0.0f);
    } else {
      Serial.println(F("[StorageMgr] LittleFS mounted successfully."));
    }
    return true;
  }

  Serial.println(F("[StorageMgr] LittleFS mount failed, attempting format..."));
  ESP.wdtDisable(); // Prevent watchdog timeout during flash format
  bool fmtOk = LittleFS.format();
  ESP.wdtFeed();

  if (fmtOk && LittleFS.begin()) {
    _mounted = true;
    Serial.println(F("[StorageMgr] LittleFS formatted and mounted successfully."));
    return true;
  }

  Serial.println(F("[StorageMgr] ERROR: LittleFS unavailable. Operating with in-memory defaults."));
  _mounted = false;
  return false;
}

bool StorageManager::format() {
  ESP.wdtDisable();
  bool ok = LittleFS.format();
  ESP.wdtFeed();
  _lastInfoUpdateMs = 0;
  if (ok) {
    _mounted = LittleFS.begin();
    Serial.println(F("[StorageMgr] Filesystem re-formatted."));
  }
  return ok;
}

bool StorageManager::readFile(const char* path, String& outContent) {
  if (!_mounted || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  outContent = f.readString();
  f.close();
  return (outContent.length() > 0);
}

bool StorageManager::writeFile(const char* path, const String& content) {
  if (!_mounted) return false;
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t bytes = f.print(content);
  f.close();
  _lastInfoUpdateMs = 0;
  return (bytes == content.length());
}

bool StorageManager::appendFile(const char* path, const String& content) {
  if (!_mounted) return false;
  File f = LittleFS.open(path, "a");
  if (!f) return false;
  size_t bytes = f.print(content);
  f.close();
  _lastInfoUpdateMs = 0;
  return (bytes == content.length());
}

bool StorageManager::exists(const char* path) {
  if (!_mounted) return false;
  return LittleFS.exists(path);
}

bool StorageManager::remove(const char* path) {
  if (!_mounted) return false;
  _lastInfoUpdateMs = 0;
  return LittleFS.remove(path);
}

size_t StorageManager::getTotalBytes() const {
  if (!_mounted) return 0;
  updateFsInfo();
  return _cachedInfo.totalBytes;
}

size_t StorageManager::getUsedBytes() const {
  if (!_mounted) return 0;
  updateFsInfo();
  return _cachedInfo.usedBytes;
}

size_t StorageManager::getFreeBytes() const {
  if (!_mounted) return 0;
  updateFsInfo();
  return (_cachedInfo.totalBytes > _cachedInfo.usedBytes) ? (_cachedInfo.totalBytes - _cachedInfo.usedBytes) : 0;
}

float StorageManager::getUsagePercent() const {
  if (!_mounted) return 0.0f;
  updateFsInfo();
  if (_cachedInfo.totalBytes == 0) return 0.0f;
  return ((float)_cachedInfo.usedBytes / (float)_cachedInfo.totalBytes) * 100.0f;
}
