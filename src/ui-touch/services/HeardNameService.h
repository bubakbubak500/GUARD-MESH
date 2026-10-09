// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/HeardNameCache.h"
#include <FS.h>
#include <atomic>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace ui { namespace services {
// One bounded staging buffer; worker never reads or modifies the live index.
class HeardNameService {
public:
  struct Host { fs::FS* (*filesystem)(); const char* (*root)(); };
  ~HeardNameService();
  void configure(Host host, unsigned capacity = HeardNameCache::MaxCapacity);
  void remember(const uint8_t key[32], const char* name, uint32_t now);
  bool lookup(const uint8_t* key, unsigned length, char* name, size_t size) const;
  int resolve(const uint8_t* key, unsigned length, uint8_t fullKey[32]) const { return _cache.resolve(key, length, fullKey); }
  unsigned count() const { return _cache.count(); }
  unsigned capacity() const { return _cache.capacity(); }
  void clear(uint32_t now);
  void tick(uint32_t now);
  bool flush(uint32_t timeoutMs = 5000);
  // Desktop/test executors call this explicitly, production has a small worker.
  bool runPending();
  bool clearing() const { return _clearRequested; }
private:
  enum State : unsigned { Idle, Queued, Running, Done };
  bool queue(bool load);
  bool readSlot(unsigned slot, uint32_t& generation, unsigned& count, bool copy);
  bool writeSnapshot();
  void path(unsigned slot, char* out, size_t size) const;
  HeardNameCache _cache;
  Host _host{};
  HeardNameCache::Record* _staging = nullptr;
  fs::FS* _filesystem = nullptr;
  char _root[24] = {};
  std::atomic<State> _state{Idle};
  std::atomic<uint32_t> _epoch{0};
  uint32_t _jobEpoch = 0, _generation = 0, _jobRevision = 0, _revision = 0;
  uint32_t _firstDirty = 0, _due = 0, _lastRecencySave = 0;
  unsigned _jobCount = 0;
  bool _loadJob = false, _ok = false, _loaded = false, _dirty = false;
  bool _clearRequested = false, _jobClear = false;
#if defined(ESP32)
  TaskHandle_t _task = nullptr;
#endif
};
} }
