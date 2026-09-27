// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace screens {
namespace releasePicker {
struct Host {
  void (*closeRoot)(lv_obj_t **);
  void (*install)(int version, bool beta);
  void (*notify)(const char *);
  lv_coord_t (*contentTop)();
  void (*focus)(lv_obj_t *);
};
void configure(const Host &);
void open(int latest, bool beta);
void close();
bool isOpen();
} // namespace releasePicker
} // namespace screens
} // namespace ui
