// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stdint.h>
namespace ui {
namespace screens {
namespace glyphPicker {
enum class Set { Emoji, Symbols };
using Result = void (*)(uint32_t, const char *);
constexpr int MotionStep = 3;
struct Host {
  int (*statusHeight)();
  void (*closeRoot)(lv_obj_t **);
  void (*insert)(lv_obj_t *target, const char *text);
  void (*returnFocus)(lv_obj_t *target);
  bool privateNavigation = false;
  bool selectFirst = false;
};
// UI-thread owner of the popup, selection and borrowed insertion target.
// A result callback receives its original request id; replacing/closing cancels it.
void configure(const Host &);
void open(lv_obj_t *target, Set set = Set::Emoji);
void pick(Result, uint32_t request, const char *title);
void cancel(Result);
void cancelFor(lv_obj_t *target);
void close();
bool isOpen();
void move(int rawDx, int rawDy);
bool activate();
} // namespace glyphPicker
} // namespace screens
} // namespace ui
