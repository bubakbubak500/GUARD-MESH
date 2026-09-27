// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
namespace widgets {
// Shared by local battery, expansion-sensor and remote telemetry charts.
void millivoltTicks(lv_event_t *);
} // namespace widgets
} // namespace ui
