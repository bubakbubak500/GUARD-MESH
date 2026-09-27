// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
namespace ui {
namespace widgets {
// Preserve the represented colour in every navigation/pressed state.
void styleColorSwatch(lv_obj_t *, uint32_t rgb, bool selected = false);
} // namespace widgets
} // namespace ui
