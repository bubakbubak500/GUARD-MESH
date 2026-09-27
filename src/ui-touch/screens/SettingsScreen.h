// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
namespace ui { namespace screens {
class SettingsScreen {
public:
  struct Category { const char* label; const char* icon; };
  static constexpr int MaxCategories = 32;
  SettingsScreen() = default;
  ~SettingsScreen() { clear(); }
  SettingsScreen(const SettingsScreen&) = delete;
  SettingsScreen& operator=(const SettingsScreen&) = delete;
  void build(lv_obj_t* tab, const Category* categories, int count, bool (*visible)(int),
             lv_event_cb_t open, void (*scrollbar)(lv_obj_t*), int about);
  void hide(int category, bool hidden);
  lv_obj_t* badge() const { return _badge; }
private:
  void clear();
  void reset();
  static void deleteEvent(lv_event_t* event);
  lv_obj_t* _root = nullptr;
  lv_obj_t* _badge = nullptr;
  lv_obj_t* _cards[MaxCategories] = {};
};
} }
