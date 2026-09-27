// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
namespace fs { class FS; }
namespace ui {
struct InstalledApp { char id[20]; char name[28]; char ver[12]; char icon[12]; };
// Current rows belong to the UI. The worker only writes a separate pending
// snapshot; poll() publishes it on the UI thread after an acquire operation.
class AppInventory {
public:
  static constexpr int Capacity = 16;
  struct Host {
    fs::FS* (*filesystem)();
    void (*path)(char*, size_t, const char*);
    int (*builtinCount)();
    InstalledApp (*builtin)(int);
    void* (*allocate)(size_t);
    void (*release)(void*);
  };
  explicit AppInventory(Host host) : _host(host) {}
  // The executor must have stopped before destruction.
  ~AppInventory();
  AppInventory(const AppInventory&) = delete;
  AppInventory& operator=(const AppInventory&) = delete;
  bool ready();
  bool prepare(bool force = false);  // UI: publish/cache/queue only; never accesses storage
  bool cancelQueued();              // worker could not start; retry on a later open
  bool request();                     // UI thread only, coalesces another scan
  bool runPending();                  // worker only, no LVGL or current-row writes
  bool poll();                        // UI thread only
  bool refresh(bool force = false);   // legacy synchronous caller; never races worker
  void invalidate();
  bool active() const { return _state.load(std::memory_order_acquire) != Idle; }
  const InstalledApp* rows() const;
  int count() const;
  const InstalledApp* find(const char*) const;
  void installed(const char* id, const char* name, const char* version);
  void removed(const char* id);      // publish a confirmed removal without a directory scan
  uint32_t revision() const { return _revision; }
private:
  enum State { Idle, Queued, Running, Ready };
  struct Snapshot { InstalledApp apps[Capacity]; int count; bool valid; };
  Host _host;
  Snapshot* _current = nullptr;
  Snapshot* _pending = nullptr;
  std::atomic<State> _state{Idle};
  bool _valid = false, _rescan = false, _discard = false;
  uint32_t _revision = 0;
  void addBuiltins(Snapshot&);
  static bool sameRows(const Snapshot&, const Snapshot&);
  void scan(Snapshot&);
  static const InstalledApp* find(const Snapshot&, const char*);
};
// Flat manifest field helper shared with the catalog parser. Output is always
// terminated; absent/empty fields return false. Kept independent of LVGL/network.
bool appManifestField(const char*, const char*, char*, size_t);
}
