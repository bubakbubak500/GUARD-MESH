// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui { namespace screens {
class QuickRepliesScreen {
public:
  struct Host { void (*syncKeyboard)(); void (*attachField)(lv_obj_t*); void (*alert)(const char*, int); };
  explicit QuickRepliesScreen(Host host) : _host(host) {}
  ~QuickRepliesScreen();
  QuickRepliesScreen(const QuickRepliesScreen&) = delete;
  QuickRepliesScreen& operator=(const QuickRepliesScreen&) = delete;
  void build(lv_obj_t* body);
private:
  static void saveEvent(lv_event_t* event);
  static void deleteEvent(lv_event_t* event);
  bool owns(lv_obj_t* object) const;
  void detachCallbacks(lv_obj_t* object);
  Host _host;
  lv_obj_t* _body = nullptr;
  lv_obj_t* _fields[TOUCH_QUICK_REPLY_COUNT] = {};
};
} }
