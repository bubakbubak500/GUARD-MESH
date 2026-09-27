// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace screens {
namespace accentCycle {
struct Host {
  void (*replace)(lv_obj_t *target, uint32_t first, uint32_t last, const char *text);
  void (*insert)(lv_obj_t *target, const char *text);
  // Layout reads only: keyboard-mode anchor or field bounds for ALT mode.
  void (*position)(lv_obj_t *popup, lv_obj_t *target, bool alt);
};
// One UI-thread owner for long-press and ALT-held cycles. Owns its popup,
// timeout and text snapshot; observes DELETE on the borrowed field.
void configure(const Host &host);
void close();
void cancelFor(lv_obj_t *target);
bool isOpen();
bool altActive();
bool longPress(lv_obj_t *target, const char *key);
// Must run AFTER the keyboard's default insertion handler. Synchronous:
// no deferred rewrite can outlive the originating field or key event.
void afterKey(lv_obj_t *target, const char *key);
// A null field continues the current ALT pick without exporting its pointer.
bool altKey(char key, lv_obj_t *target = nullptr);
void altReleased();
} // namespace accentCycle
} // namespace screens
} // namespace ui
