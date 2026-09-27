// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include <stddef.h>
#include <stdint.h>
namespace ui {
namespace screens {
// Owns row callbacks and render state; the surrounding tab lends its list.
// Rebinding/destruction retires old callbacks without deleting the borrowed tab.
class ThreadListScreen {
public:
  struct Host {
    int (*indexes)(bool channel, bool combined, int *, int);
    bool (*info)(int, bool &, uint16_t &, uint32_t &, char *, size_t);
    bool (*mention)(int);
    bool (*lastMessage)(int, char *, size_t, char *, size_t, bool *);
    bool (*compact)();
    bool (*emoji)(const char *, char *, size_t);
    uint8_t (*scale)();
    void (*sanitize)(const lv_font_t *, char *, size_t, const char *);
    void (*select)(int, bool);
    void (*actions)(int, const char *, bool);
    // Optional hardware hold sampler for e-paper touch; null uses LVGL long press.
    uint32_t (*holdMilliseconds)();
    bool (*swiping)();
  };
  explicit ThreadListScreen(Host host) : _host(host) {}
  ~ThreadListScreen();
  ThreadListScreen(const ThreadListScreen &) = delete;
  ThreadListScreen &operator=(const ThreadListScreen &) = delete;
  void refresh(lv_obj_t *list, bool channel, bool combined);

private:
  struct Action;
  Host _host;
  widgets::ObjectRef _list;
  Action *_actions = nullptr;
  uint32_t _signature = 0;
  bool _channel = false, _combined = false;
  void detach();
  bool current(const Action &) const;
  static void rowEvent(lv_event_t *);
  static void gearEvent(lv_event_t *);
  static void deleted(lv_event_t *);
  static void dispatch(Action *, bool menu);
};
} // namespace screens
} // namespace ui
