// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
namespace ui {
namespace screens {
namespace quickReplyPicker {
struct Host {
  int (*statusHeight)();
  void (*closeRoot)(lv_obj_t **);
  int (*replyCount)();
  int (*readReply)(int, char *, size_t);
  bool (*position)(double &, double &);
  void (*insert)(lv_obj_t *, const char *);
};
// Owns popup, immutable reply snapshot and a watched insertion target. GPS is
// rechecked at selection; a lost fix never inserts the stale displayed position.
void configure(const Host &);
void open(lv_obj_t *target);
void close();
void cancelFor(lv_obj_t *target);
bool isOpen();
} // namespace quickReplyPicker
} // namespace screens
} // namespace ui
