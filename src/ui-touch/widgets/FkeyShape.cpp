// SPDX-License-Identifier: GPL-3.0-or-later
#include "FkeyShape.h"
#include "../platform/UiPlatform.h"
namespace ui {
namespace widgets {
// Draw one coloured F-key OUTLINE shape (△ □ ○ ⏢ ◇) into a fresh SZ×SZ canvas child of `parent`,
// centred. Mirrors the bottom tab-bar key hints (navBuildTabKeyHints) so the in-chat chips read as
// "press the same-coloured hardware key". Returns the canvas (non-clickable).
static lv_obj_t *makeFkeyShape(lv_obj_t *parent, int shape, uint32_t rgb, int SZ) {
  lv_color_t col = lv_color_hex(rgb);
  lv_obj_t *cv = lv_canvas_create(parent);
  lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
  uint8_t *buf =
      static_cast<uint8_t *>(platform::allocate(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(SZ, SZ), true));
  if (!buf) {
    lv_obj_del(cv);
    return nullptr;
  }
  if (!lv_obj_add_event_cb(
          cv, [](lv_event_t *event) { platform::release(lv_event_get_user_data(event)); }, LV_EVENT_DELETE,
          buf)) {
    platform::release(buf);
    lv_obj_del(cv);
    return nullptr;
  }
  lv_canvas_set_buffer(cv, buf, SZ, SZ, LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = col;
  ld.width = 2;
  ld.round_start = ld.round_end = 1;
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_opa = LV_OPA_TRANSP;
  rd.border_color = col;
  rd.border_width = 2;
  rd.border_opa = LV_OPA_COVER;
  const lv_coord_t hh = (lv_coord_t)(SZ / 2);
  const lv_coord_t ed = (lv_coord_t)(SZ - 2);
  const lv_coord_t q1 = (lv_coord_t)(SZ / 4);
  const lv_coord_t q3 = (lv_coord_t)(SZ - SZ / 4);
  const lv_coord_t e3 = (lv_coord_t)(SZ - 3);
  switch (shape) {
  case 0: {
    lv_point_t p[4] = {{hh, 1}, {1, ed}, {ed, ed}, {hh, 1}};
    lv_canvas_draw_line(cv, p, 4, &ld);
  } break; // △
  case 1:
    rd.radius = 4;
    lv_canvas_draw_rect(cv, 1, 1, SZ - 2, SZ - 2, &rd);
    break; // □
  case 2:
    rd.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, 1, 1, SZ - 2, SZ - 2, &rd);
    break; // ○
  case 3: {
    lv_point_t p[5] = {{q1, 2}, {q3, 2}, {ed, e3}, {2, e3}, {q1, 2}};
    lv_canvas_draw_line(cv, p, 5, &ld);
  } break; // ⏢ trapezoid
  default: {
    lv_point_t p[5] = {{hh, 1}, {ed, hh}, {hh, ed}, {1, hh}, {hh, 1}};
    lv_canvas_draw_line(cv, p, 5, &ld);
  } break; // ◇
  }
  lv_obj_align(cv, LV_ALIGN_CENTER, 0, 0);
  return cv;
}
// Re-skin an existing composer chip / floating button as a coloured F-key shape: strip its round
// fill, draw the shape behind the icon, and tint a monochrome icon to match (a colour-emoji icon
// keeps its own colours → pass tint_icon=false).
void styleChipAsFkey(lv_obj_t *btn, lv_obj_t *icon, int shape, uint32_t rgb, int SZ, bool tint_icon) {
  if (!btn)
    return;
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
  makeFkeyShape(btn, shape, rgb, SZ);
  if (icon) {
    lv_obj_move_foreground(icon); // keep the glyph above the shape
    if (shape == 0)
      lv_obj_align(icon, LV_ALIGN_CENTER, 0,
                   SZ / 6); // △: drop the glyph into the wider lower body so it fits
    if (tint_icon)
      lv_obj_set_style_text_color(icon, lv_color_hex(rgb), LV_PART_MAIN);
  }
}

} // namespace widgets
} // namespace ui
