// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace widgets {
// Shares the original boot/app identity; no font glyph substitution.
lv_obj_t* guardianShield(lv_obj_t* parent, int size);
} }
