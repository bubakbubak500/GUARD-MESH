// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stdint.h>
namespace ui {
// One UI producer/consumer, one worker. UI keeps reading its previous snapshot
// until poll publishes a complete new one. Stop the executor before destruction.
class WifiScanJob {
public:
  static constexpr int Capacity = 14, SsidCapacity = 32;
  struct Snapshot {
    char ssids[Capacity][SsidCapacity]{};
    int count = 0;
    int16_t kick = -9, status = -9;
    uint32_t duration = 0;
    uint8_t radioStatus = 0;
    bool available = false;
    bool add(const char *ssid);
  };
  using Scan = void (*)(void *, Snapshot &);
  bool request();    // UI
  bool cancel();     // UI: succeeds only before the worker claims the request
  void failQueued(); // UI: unavailable executor, published as an empty result
  bool poll();       // UI only: publish and consume completion
  bool active() const;
  const Snapshot &snapshot() const { return _snapshot; } // UI only
  bool run(void *context, Scan);                         // one worker; no LVGL or UI-owned buffers
private:
  enum State { Idle, Queued, Running, Ready };
  std::atomic<State> _state{Idle};
  Snapshot _snapshot{}, _result{};
};
} // namespace ui
