// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stdint.h>
namespace ui {
// UI owns the published snapshot and scheduling. A single worker computes into
// separate storage. Mount lifecycle waits for busy(), then calls invalidate().
class StorageUsage {
public:
  struct Snapshot {
    bool done = false, ok = false;
    uint64_t total = 0, free = 0;
  };
  typedef Snapshot (*Read)(void *);
  bool refresh(uint32_t now, bool (*ensureExecutor)()); // UI, 30-second interval
  void poll();       // UI: consume results and mount invalidations, cancel stale queued work
  void invalidate(); // any thread: notification only, never mutates UI-owned data
  const Snapshot &snapshot() {
    poll();
    return _snapshot;
  }                              // UI only
  bool busy() const;             // queued/running; safe for lifecycle guards
  bool run(void *context, Read); // single worker, synchronous read
private:
  enum State { Idle, Queued, Running, Ready };
  std::atomic<State> _state{Idle};
  std::atomic<uint32_t> _mountGeneration{0};
  uint32_t _generation = 0, _requestGeneration = 0, _lastRequest = 0;
  bool _scheduled = false;
  Snapshot _snapshot{}, _result{};
};
} // namespace ui
