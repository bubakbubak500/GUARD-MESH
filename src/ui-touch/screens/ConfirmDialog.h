// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include <functional>
namespace ui {
namespace screens {
// The owner outlives its LVGL tree. DELETE clears the current root; events from
// a replaced tree cannot invoke the new tree's confirmation callback.
class ConfirmDialog {
public:
  using Action = void (*)();
  using CapturedAction = std::function<void()>;
  struct Host {
    lv_coord_t (*contentTop)();
    void (*closeRoot)(lv_obj_t **);
    void (*focus)(lv_obj_t *);
  };
  explicit ConfirmDialog(Host host) : _host(host) {}
  ~ConfirmDialog() { dismiss(); }
  ConfirmDialog(const ConfirmDialog &) = delete;
  ConfirmDialog &operator=(const ConfirmDialog &) = delete;
  void show(const char *message, const char *ok_label, Action action, bool actions_only_nav);
  // Own value captures through dismissal; never resolve a confirmed operation
  // from a mutable global selection after closeRoot has called external code.
  void showCaptured(const char *message, const char *ok_label, CapturedAction action, bool actions_only_nav);
  void dismiss();
  bool isOpen() const { return _root != nullptr; }

private:
  bool owns(lv_obj_t *object) const;
  void detachCallbacks(lv_obj_t *object);
  static void cancelEvent(lv_event_t *event);
  static void acceptEvent(lv_event_t *event);
  static void deleteEvent(lv_event_t *event);
  Host _host;
  lv_obj_t *_root = nullptr;
  CapturedAction _action;
  uint32_t _generation = 0;
};
} // namespace screens
} // namespace ui
