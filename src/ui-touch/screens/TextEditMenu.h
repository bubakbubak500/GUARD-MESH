// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
namespace ui {
namespace screens {
namespace textEditMenu {
struct Host {
  // resolve/clipboard/paste/bounds are synchronous reads; they must not mutate UI.
  void (*closeRoot)(lv_obj_t **root);
  lv_obj_t *(*resolve)(lv_obj_t *field);
  const char *(*clipboard)();
  void (*copy)(const char *text, bool cut);
  const char *(*paste)(lv_obj_t *field, const char *text, char *scratch, size_t capacity);
  void (*symbols)(lv_obj_t *field);
  void (*bounds)(lv_coord_t &top, lv_coord_t &bottom);
};
// UI-thread owner of the floating edit menu and captured edit context. Both
// the originating field and resolved mirror are observed until dismissal.
void configure(const Host &host);
bool show(lv_obj_t *field);
void close();
void cancelFor(lv_obj_t *field);
bool isOpen();
} // namespace textEditMenu
} // namespace screens
} // namespace ui
