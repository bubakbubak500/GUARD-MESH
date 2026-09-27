// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace widgets {
namespace textSelection {
// UI-thread selection state. The LVGL 8 textarea/label compatibility bridge is
// confined here; callers never access sel_start/sel_end directly.
uint32_t cpToByte(const char *text, uint32_t position);
uint32_t byteToCp(const char *text, uint32_t position);
void select(lv_obj_t *field, uint32_t first, uint32_t last);
void clear(lv_obj_t *field);
bool range(lv_obj_t *field, uint32_t *first, uint32_t *last);
void restore(lv_obj_t *field);
void selectWord(lv_obj_t *field);
// Unrestricted fields use one VALUE_CHANGED operation. Restricted/password
// fields retain native LVGL insertion rules, checking lifetime after each step.
// False means failure, field deletion,
// or a reentrant edit superseding this one;
// never dereference a borrowed field after this call without checking it.
bool erase(lv_obj_t *field, uint32_t first, uint32_t last);
// An optional operation guard cancels a multi-character edit when its owning
// binding changes inside a synchronous LVGL callback. It must be read-only.
using StillCurrent = bool (*)(void *);
bool replace(lv_obj_t *field, uint32_t first, uint32_t last, const char *replacement,
             StillCurrent current = nullptr, void *context = nullptr);
void clicked(lv_obj_t *field, uint32_t milliseconds);
void reset(); // detach remembered selection and gesture targets, without deleting widgets
} // namespace textSelection
} // namespace widgets
} // namespace ui
