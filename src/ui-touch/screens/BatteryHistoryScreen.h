// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/BatteryHistory.h"
#include "../widgets/ObjectRef.h"
#include "ConfirmDialog.h"
namespace ui {
namespace screens {
class BatteryHistoryScreen {
public:
  struct Snapshot {
    uint16_t fullMv = 4200, cpuMHz = 0;
    bool sleeping = false;
    uint32_t wakes = 0;
    unsigned asleepPercent = 0;
    char lastWake[64]{}, blocker[64]{};
  };
  struct Host {
    void *context;
    void (*snapshot)(void *, Snapshot &);
    void (*alert)(void *, const char *, int);
    lv_coord_t (*contentTop)();
    void (*closeRoot)(lv_obj_t **);
    void (*focus)(lv_obj_t *);
    void *(*allocate)(size_t);
    void (*release)(void *);
  };
  BatteryHistoryScreen(BatteryHistory &history, Host host)
      : _history(history), _host(host), _confirm({host.contentTop, host.closeRoot, host.focus}) {}
  ~BatteryHistoryScreen() { close(); }
  BatteryHistoryScreen(const BatteryHistoryScreen &) = delete;
  BatteryHistoryScreen &operator=(const BatteryHistoryScreen &) = delete;
  void open();
  void close();
  bool isOpen() const { return _root.get() || _confirm.isOpen(); }

private:
  static void event(lv_event_t *);
  static void deleted(lv_event_t *);
  bool owns(lv_obj_t *) const;
  void detachCallbacks(lv_obj_t *);
  void clearConfirmed(BatteryHistory::Backend, uint32_t);
  void reopen(uint32_t);
  void alert(const char *);
  BatteryHistory &_history;
  Host _host;
  ConfirmDialog _confirm;
  BatteryHistory::Backend _backend;
  widgets::ObjectRef _root;
  uint32_t _generation = 0;
  bool _showCpu = true;
};
} // namespace screens
} // namespace ui
