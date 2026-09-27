// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace screens {
// Owns a fullscreen tool shell and copied title. Content owners release their
// callbacks/resources through releaseContents before normal or external DELETE.
class FullscreenToolView {
public:
  struct Host {
    int (*statusHeight)();
    void (*closePopup)(lv_obj_t**);
    void (*releaseContents)();
    void (*changed)();
    void (*home)();
  };
  explicit FullscreenToolView(Host host) : _host(host) {}
  ~FullscreenToolView() { close(); }
  FullscreenToolView(const FullscreenToolView&) = delete;
  FullscreenToolView& operator=(const FullscreenToolView&) = delete;
  lv_obj_t* open(const char* title);
  void close();
  lv_obj_t* root() const { return _root; }
  const char* title() const { return _title; }
private:
  Host _host;
  lv_obj_t* _root = nullptr;
  char _title[40] = {};
  static void deleted(lv_event_t*);
  void detach(lv_obj_t*);
};
} }
