// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace screens {
namespace accentPicker {
struct Host {
  void (*apply)(lv_obj_t *target, const char *text, uint32_t cursor);
  void (*bounds)(lv_coord_t &top, lv_coord_t &bottom);
};
// Passive suggestions for the character before the caret. UI-thread singleton;
// owns the popup and text snapshot, observes the borrowed field's DELETE.
void configure(const Host &host);
bool show(lv_obj_t *target);
void close();
void cancelFor(lv_obj_t *target);
bool isOpen();
bool navigationActive();
bool enterNavigation();
void move(int delta);
void confirm();
} // namespace accentPicker
} // namespace screens
} // namespace ui
