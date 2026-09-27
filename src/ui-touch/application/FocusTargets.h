// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
namespace ui {
// Owns a navigation group and its ordered targets. Widgets are borrowed; DELETE
// removes them before LVGL tears down styles and auto-focuses a surviving target.
class FocusTargets {
public:
  using Deleted = void (*)(lv_obj_t *);
  explicit FocusTargets(Deleted deleted = nullptr) : _deleted(deleted) {}
  ~FocusTargets();
  FocusTargets(const FocusTargets &) = delete;
  FocusTargets &operator=(const FocusTargets &) = delete;
  bool initialize();
  bool add(lv_obj_t *);
  void clear();
  lv_group_t *group() const { return _group; } // borrowed, do not destroy/add/remove targets directly
  int count() const { return static_cast<int>(_count); }
  lv_obj_t *at(int index) const;

private:
  static void deletedEvent(lv_event_t *);
  void remove(lv_obj_t *);
  lv_group_t *_group = nullptr;
  lv_obj_t **_objects = nullptr;
  size_t _count = 0, _capacity = 0;
  Deleted _deleted;
};
} // namespace ui
