// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorSwatch.h"
#include "../theme/Theme.h"
namespace ui {
namespace widgets {
void styleColorSwatch(lv_obj_t *object, uint32_t rgb, bool selected) {
  const lv_style_selector_t states[] = {LV_PART_MAIN, LV_PART_MAIN | LV_STATE_FOCUSED,
                                        LV_PART_MAIN | LV_STATE_FOCUS_KEY, LV_PART_MAIN | LV_STATE_PRESSED,
                                        LV_PART_MAIN | LV_STATE_CHECKED};
  for (auto state : states) {
    lv_obj_set_style_bg_color(object, lv_color_hex(rgb), state);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, state);
  }
  lv_obj_set_style_radius(object, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, selected ? 2 : 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(selected ? 0xFFFFFF : theme::colors().COLOR_BORDER),
                                LV_PART_MAIN);
}
} // namespace widgets
} // namespace ui
