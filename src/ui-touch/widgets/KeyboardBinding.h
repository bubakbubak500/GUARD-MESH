// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace widgets {
// Owns the mirror strip; borrows the keyboard and target field with DELETE
// hooks. Device policy decides whether a binding needs an on-screen mirror.
class KeyboardBinding {
public:
  explicit KeyboardBinding(bool (*ready)()=nullptr) : _ready(ready) {}
  ~KeyboardBinding();
  KeyboardBinding(const KeyboardBinding&) = delete;
  KeyboardBinding& operator=(const KeyboardBinding&) = delete;
  void ensureCreated(lv_coord_t top,lv_coord_t height);
  bool bind(lv_obj_t* keyboard,lv_obj_t* target,bool mirrored);
  void sync();
  void setText(lv_obj_t* target,const char* text);
  void clear(); // detach without committing; callers choose when to sync
  void resize(lv_coord_t width);
  void show(lv_coord_t top);
  void hide();
  lv_obj_t* target() const { return _target; }
  lv_obj_t* mirror() const { return _mirror; }
  lv_obj_t* root() const { return _root; }
private:
  lv_obj_t* _root=nullptr;
  lv_obj_t* _mirror=nullptr;
  lv_obj_t* _target=nullptr;
  lv_obj_t* _keyboard=nullptr;
  bool (*_ready)();
  bool _mirrored=false,_syncing=false;
  uint32_t _generation=0;
  static void changed(lv_event_t*);
  static void deleted(lv_event_t*);
  void watch(lv_obj_t*);
  void unwatch(lv_obj_t*);
};
} }
