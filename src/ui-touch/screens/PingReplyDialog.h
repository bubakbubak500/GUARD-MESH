// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include "../models/PingStatus.h"
namespace ui { namespace screens {
class PingReplyDialog {
public:
  struct Host {
    lv_coord_t (*contentTop)();
    void (*closeRoot)(lv_obj_t**);
    void (*focus)(lv_obj_t*);
  };
  explicit PingReplyDialog(Host host) : _host(host) {}
  ~PingReplyDialog() { dismiss(); }
  PingReplyDialog(const PingReplyDialog&) = delete;
  PingReplyDialog& operator=(const PingReplyDialog&) = delete;
  void show(const char* name, const PingStatus& status);
  void dismiss();
  bool isOpen() const { return _root != nullptr; }
private:
  static void closeEvent(lv_event_t* e);
  static void backdropEvent(lv_event_t* e);
  static void deleteEvent(lv_event_t* e);
  void detach(lv_obj_t* object);
  Host _host;
  lv_obj_t* _root = nullptr;
};
} }
