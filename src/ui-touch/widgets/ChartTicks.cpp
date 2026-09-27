// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChartTicks.h"
namespace ui {
namespace widgets {
void millivoltTicks(lv_event_t *event) {
  auto *part = lv_event_get_draw_part_dsc(event);
  if (!part || !lv_obj_draw_part_check_type(part, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL) ||
      !part->text)
    return;
  if (part->id == LV_CHART_AXIS_PRIMARY_Y)
    lv_snprintf(part->text, part->text_length, "%d.%01d", int(part->value / 1000),
                int(part->value % 1000 / 100));
}
} // namespace widgets
} // namespace ui
