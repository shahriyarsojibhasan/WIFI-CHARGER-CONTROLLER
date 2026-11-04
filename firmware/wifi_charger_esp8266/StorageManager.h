#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>

class StorageManager {
public:
  StorageManager();

  bool begin();
  bool format();

  bool readFile(const char* path, String& outContent);
  bool writeFile(const char* path, const String& content);
  bool appendFile(const char* path, const String& content);
  bool exists(const char* path);
  bool remove(const char* path);

  size_t getTotalBytes() const;
  size_t getUsedBytes() const;
  size_t getFreeBytes() const;
  float  getUsagePercent() const;
  bool   isMounted() const { return _mounted; }

private:
  void updateFsInfo() const;

  bool _mounted;
  mutable FSInfo _cachedInfo;
  mutable uint32_t _lastInfoUpdateMs;
};

extern StorageManager storageMgr;

#endif // STORAGE_MANAGER_H
