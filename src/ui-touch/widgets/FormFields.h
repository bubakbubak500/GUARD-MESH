// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace widgets {
bool parseFloatField(lv_obj_t*, float&);
bool parseIntField(lv_obj_t*, int&);
} }
