// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
namespace ui { namespace screens {
class AppPermissionsScreen {
public:
  using RowLabel = int (*)(lv_obj_t*, int, int, const char*, uint32_t, const lv_font_t*, int);
  explicit AppPermissionsScreen(RowLabel label) : _label(label) {}
  ~AppPermissionsScreen();
  AppPermissionsScreen(const AppPermissionsScreen&) = delete;
  AppPermissionsScreen& operator=(const AppPermissionsScreen&) = delete;
  void build(lv_obj_t* page, lv_coord_t label_width);
private:
  static void toggle(lv_event_t* event);
  static void deleted(lv_event_t* event);
  bool owns(lv_obj_t* object) const;
  void detach(lv_obj_t* object);
  struct Permission { AppPermissionsScreen* owner; uint8_t index; uint8_t bit; };
  RowLabel _label;
  lv_obj_t* _page = nullptr;
  char _ids[12][24] = {};
  Permission _permissions[12][5] = {};
};
} }
