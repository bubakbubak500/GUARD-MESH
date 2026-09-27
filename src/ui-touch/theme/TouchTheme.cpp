// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchTheme.h"
#include "../device_caps.h"
#include "../widgets/Styles.h"
#include "Theme.h"
namespace ui {
namespace theme {
using namespace widgets;
static void touchThemeApplyCb(lv_theme_t * /*th*/, lv_obj_t *obj) {
  if (lv_obj_check_type(obj, &lv_switch_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
  }
  if (!isDay())
    return;
#if defined(HAS_TDECK_PRO)
  if (lv_obj_check_type(obj, &lv_textarea_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_CURSOR);
  } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
  } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
  } else if (lv_obj_check_type(obj, &lv_checkbox_class)) {
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
    styleEpaperControlOutline(obj, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  } else if (lv_obj_check_type(obj, &lv_switch_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_KNOB | LV_STATE_CHECKED);
    styleEpaperControlOutline(obj, LV_PART_KNOB);
    lv_obj_set_style_anim_time(obj, 0, LV_PART_MAIN);
  } else if (lv_obj_check_type(obj, &lv_slider_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_KNOB);
    styleEpaperControlOutline(obj, LV_PART_KNOB);
  } else if (lv_obj_check_type(obj, &lv_btn_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
    styleEpaperControlOutline(obj, LV_PART_MAIN);
  }
  return;
#endif
  if (lv_obj_check_type(obj, &lv_textarea_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors().COLOR_SUB), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_CURSOR);
  } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  } else if (lv_obj_check_type(obj, &lv_slider_class)) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_KNOB);
  } else if (lv_obj_check_type(obj, &lv_checkbox_class)) {
    lv_obj_set_style_border_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(obj, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  }
}

void TouchTheme::install(lv_disp_t *display) {
  if (!display)
    return;
  auto *base = lv_disp_get_theme(display);
  if (!base || base == &_theme)
    return; // Do not form a parent cycle on repeated install.
  _theme = *base;
  lv_theme_set_parent(&_theme, base);
  lv_theme_set_apply_cb(&_theme, touchThemeApplyCb);
  lv_disp_set_theme(display, &_theme);
}
} // namespace theme
} // namespace ui
