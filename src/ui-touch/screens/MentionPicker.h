// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace screens {
namespace mentionPicker {
constexpr int SourceLimit = 16;
constexpr int NameBytes = 32;
struct Host {
  int (*names)(char out[][NameBytes], int capacity);
  void (*apply)(lv_obj_t *target, const char *text, uint32_t cursor);
  // Vertical limits supplied by the keyboard/composer layout owner.
  void (*bounds)(lv_coord_t &top, lv_coord_t &bottom);
  bool navigation;
};
// One UI-thread instance. Owns the popup and copied suggestions; borrows the
// field with DELETE observation. No radio, keyboard or preference dependencies.
void configure(const Host &host);
bool show(lv_obj_t *target);
void close();
void cancelFor(lv_obj_t *target);
bool isOpen();
bool navigationActive();
void move(int delta);
void confirm();
} // namespace mentionPicker
} // namespace screens
} // namespace ui
