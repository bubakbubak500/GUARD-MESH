// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeardNameService.h"
#include "../platform/UiPlatform.h"
#include "../platform/StorageAccess.h"
#include <cstdio>
#include <cstring>
namespace ui { namespace services {
namespace {
constexpr uint32_t Magic = 0x314e4847u;
struct Header { uint32_t magic, version, generation, count, crc; };
uint32_t crcBytes(uint32_t crc, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (size--) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return crc;
}
}
HeardNameService::~HeardNameService() {
#if defined(ESP32)
  while (_state.load() == Running) vTaskDelay(1);
  if (_task) vTaskDelete(_task);
#endif
  platform::release(_staging);
}
void HeardNameService::configure(Host host, unsigned capacity) {
  _host = host;
  if (!_cache.capacity()) {
    while (capacity >= 64 && !_cache.allocate(capacity, platform::allocate, platform::release)) capacity /= 2;
  }
  if (!_staging && _cache.capacity())
    _staging = static_cast<HeardNameCache::Record*>(platform::allocate(sizeof(HeardNameCache::Record) * _cache.capacity(), true));
  if (!_staging && _cache.capacity())
    _staging = static_cast<HeardNameCache::Record*>(platform::allocate(sizeof(HeardNameCache::Record) * _cache.capacity(), false));
}
void HeardNameService::remember(const uint8_t key[32], const char* name, uint32_t now) {
  const bool changed = _cache.remember(key, name);
  if (!name || !name[0] || !_cache.capacity()) return;
  // Recency is checkpointed at most once per minute; names are saved promptly.
  if (!changed && static_cast<uint32_t>(now - _lastRecencySave) < 60000) return;
  ++_revision;
  if (!_dirty) _firstDirty = now;
  _dirty = true; _due = now + 20000;
  if (!changed) _lastRecencySave = now;
}
bool HeardNameService::lookup(const uint8_t* key, unsigned length, char* name, size_t size) const {
  return _cache.lookup(key, length, name, size);
}
void HeardNameService::clear(uint32_t now) {
  ++_epoch; ++_revision; _cache.clear();
  _loaded = true; _clearRequested = _dirty = true; _firstDirty = now; _due = now;
}
void HeardNameService::path(unsigned slot, char* out, size_t size) const {
  snprintf(out, size, "%s/heard-names.%u", _root, slot);
}
bool HeardNameService::queue(bool load) {
  if (!_staging || !_host.filesystem || !_host.root || _state.load() != Idle) return false;
  _filesystem = _host.filesystem();
  if (!_filesystem) return false;
  snprintf(_root, sizeof _root, "%s", _host.root());
  _loadJob = load; _jobEpoch = _epoch.load(); _jobRevision = _revision; _jobClear = _clearRequested;
  _jobCount = load ? 0 : _cache.snapshot(_staging, _cache.capacity());
  _state.store(Queued, std::memory_order_release);
#if defined(ESP32)
  if (!_task && xTaskCreate([](void* context) {
        auto& service = *static_cast<HeardNameService*>(context);
        for (;;) { service.runPending(); vTaskDelay(pdMS_TO_TICKS(20)); }
      }, "heard_names", 4096, this, 1, &_task) != pdPASS) {
    _state.store(Idle); return false;
  }
#endif
  return true;
}
bool HeardNameService::readSlot(unsigned slot, uint32_t& generation, unsigned& count, bool copy) {
  char filename[64]; path(slot, filename, sizeof filename);
  File file = _filesystem->open(filename, FILE_READ);
  Header header{};
  if (!file || file.read(reinterpret_cast<uint8_t*>(&header), sizeof header) != sizeof header ||
      header.magic != Magic || header.version != 1 || header.count > HeardNameCache::MaxCapacity ||
      file.size() != sizeof header + header.count * sizeof(HeardNameCache::Record)) return false;
  uint32_t crc = crcBytes(~0u, &header, offsetof(Header, crc));
  unsigned destination = 0;
  for (unsigned i = 0; i < header.count; ++i) {
    HeardNameCache::Record record;
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof record) != sizeof record ||
        !record.name[0] || !memchr(record.name, 0, sizeof record.name)) return false;
    crc = crcBytes(crc, &record, sizeof record);
    if (copy && i + _cache.capacity() >= header.count) _staging[destination++] = record;
  }
  if (~crc != header.crc) return false;
  generation = header.generation; count = copy ? destination : header.count;
  return true;
}
bool HeardNameService::writeSnapshot() {
  if (_epoch.load() != _jobEpoch) return false;
  const uint32_t next = _generation + 1;
  Header header{Magic, 1, next, _jobCount, 0};
  uint32_t crc = crcBytes(~0u, &header, offsetof(Header, crc));
  header.crc = ~crcBytes(crc, _staging, _jobCount * sizeof(HeardNameCache::Record));
  char filename[64]; path(next & 1, filename, sizeof filename);
  File file = _filesystem->open(filename, FILE_WRITE);
  if (!file || file.write(reinterpret_cast<const uint8_t*>(&header), sizeof header) != sizeof header) return false;
  const auto* bytes = reinterpret_cast<const uint8_t*>(_staging);
  size_t left = _jobCount * sizeof(HeardNameCache::Record);
  while (left) {
    size_t amount = left > 1024 ? 1024 : left;
    if (_epoch.load() != _jobEpoch || file.write(bytes, amount) != amount) return false;
    bytes += amount; left -= amount;
  }
  file.flush(); file.close();
  uint32_t verified = 0; unsigned count = 0;
  if (_epoch.load() != _jobEpoch || !readSlot(next & 1, verified, count, false) || verified != next) return false;
  _generation = next;
  if (_jobClear) {
    // An empty/new snapshot is durable first; remove the pre-clear fallback.
    path((next & 1) ^ 1, filename, sizeof filename);
    if (_filesystem->exists(filename) && !_filesystem->remove(filename)) return false;
  }
  return true;
}
bool HeardNameService::runPending() {
  State expected = Queued;
  if (!_state.compare_exchange_strong(expected, Running, std::memory_order_acq_rel)) return false;
  platform::StorageLease lease;
  _ok = false;
  if (lease) {
    if (_loadJob) {
      uint32_t a = 0, b = 0; unsigned ac = 0, bc = 0;
      const bool av = readSlot(0, a, ac, false), bv = readSlot(1, b, bc, false);
      if (av || bv) {
        unsigned slot = bv && (!av || static_cast<int32_t>(b - a) > 0) ? 1 : 0;
        _ok = readSlot(slot, _generation, _jobCount, true);
      } else { _jobCount = 0; _ok = true; }
    } else _ok = writeSnapshot();
  }
  _state.store(Done, std::memory_order_release); return true;
}
void HeardNameService::tick(uint32_t now) {
  if (!_staging) return; // RAM-only fallback; do not repeatedly touch storage on OOM.
  if (_state.load(std::memory_order_acquire) == Done) {
    if (_loadJob) {
      if (_ok && _epoch.load() == _jobEpoch) {
        // Live adverts win over older records loaded on the worker.
        const unsigned liveCount = _cache.count();
        auto* live = static_cast<HeardNameCache::Record*>(platform::allocate(liveCount * sizeof(HeardNameCache::Record), true));
        if (!live && liveCount) live = static_cast<HeardNameCache::Record*>(platform::allocate(liveCount * sizeof(HeardNameCache::Record), false));
        if (!liveCount || live) {
          _cache.snapshot(live, liveCount); _cache.clear();
          for (unsigned i = 0; i < _jobCount; ++i) _cache.remember(_staging[i].key, _staging[i].name);
          for (unsigned i = 0; i < liveCount; ++i) _cache.remember(live[i].key, live[i].name);
        }
        platform::release(live);
        _loaded = true;
      }
    } else if (_ok && _epoch.load() == _jobEpoch) {
      if (_jobRevision == _revision) _dirty = false;
      if (_jobClear) _clearRequested = false;
    }
    _state.store(Idle, std::memory_order_release);
    if (!_ok) _due = now + 5000;
  }
  if (_state.load() != Idle) return;
  if (!_loaded) {
    if (static_cast<int32_t>(now - _due) >= 0 && !queue(true)) _due = now + 5000;
    return;
  }
  if (_dirty && (static_cast<int32_t>(now - _due) >= 0 || static_cast<uint32_t>(now - _firstDirty) >= 60000)) {
    if (queue(false)) { _firstDirty = now; _due = now + 20000; }
    else _due = now + 5000;
  }
}
bool HeardNameService::flush(uint32_t timeoutMs) {
  const uint32_t start = platform::milliseconds();
  _due = start;
  do {
    tick(platform::milliseconds());
#if !defined(ESP32)
    runPending();
#endif
    if (_loaded && !_dirty && _state.load() == Idle) return true;
    platform::yieldUiWork();
  } while (static_cast<uint32_t>(platform::milliseconds() - start) < timeoutMs);
  return false;
}
} }
