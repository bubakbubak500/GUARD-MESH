// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace widgets {
// UI-thread-only borrowed reference. Follows DELETE, including while its widget
// is outside the active navigation group. Never deletes the widget itself.
class ObjectRef {
public:
  ObjectRef() = default;
  ~ObjectRef() { set(nullptr); }
  ObjectRef(const ObjectRef &) = delete;
  ObjectRef &operator=(const ObjectRef &) = delete;
  bool set(lv_obj_t *object) {
    if (object == _object)
      return true;
    if (_object)
      lv_obj_remove_event_cb_with_user_data(_object, deleted, this);
    _object = object;
    if (_object && !lv_obj_add_event_cb(_object, deleted, LV_EVENT_DELETE, this)) {
      _object = nullptr;
      return false;
    }
    return true;
  }
  lv_obj_t *get() const { return _object; }

private:
  static void deleted(lv_event_t *event) {
    auto *self = static_cast<ObjectRef *>(lv_event_get_user_data(event));
    if (lv_event_get_target(event) == self->_object)
      self->_object = nullptr;
  }
  lv_obj_t *_object = nullptr;
};
} // namespace widgets
} // namespace ui
