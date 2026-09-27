// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace widgets {
// Canvas storage belongs to the child and is released on DELETE.
void styleChipAsFkey(lv_obj_t *button, lv_obj_t *icon, int shape, uint32_t rgb, int size, bool tintIcon);
} // namespace widgets
} // namespace ui
