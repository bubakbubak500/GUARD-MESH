// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianShield.h"
#include "../../guardian_logo.h"
namespace ui { namespace widgets {
lv_obj_t* guardianShield(lv_obj_t* parent, int size) {
  static const lv_img_dsc_t picture = [] {
    lv_img_dsc_t d{}; d.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    d.header.w = GUARDIAN_LOGO_W; d.header.h = GUARDIAN_LOGO_H;
    d.data_size = sizeof GUARDIAN_LOGO_ALPHA; d.data = GUARDIAN_LOGO_ALPHA; return d;
  }();
  auto* box = lv_obj_create(parent); lv_obj_remove_style_all(box);
  lv_obj_set_size(box, size, size);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  auto* image = lv_img_create(box); lv_img_set_src(image, &picture);
  lv_img_set_pivot(image, 0, 0); lv_img_set_zoom(image, size * 256 / GUARDIAN_LOGO_W);
  lv_obj_set_pos(image, 0, 0); lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
  return box;
}
} }
