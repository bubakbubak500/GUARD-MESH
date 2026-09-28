// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace widgets {
enum class GuardianIcon { Mail, Network, Pencil, Send, Bookmark, Down, Up, Person, Gear, Home, Chevron };
// Resolution-independent A3 artwork. Uses the app accent; no font glyph substitutes.
lv_obj_t* guardianIcon(lv_obj_t* parent, GuardianIcon icon, int size, uint32_t accent);
} }
