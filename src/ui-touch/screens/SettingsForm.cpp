// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsForm.h"
#include "../theme/Fonts.h"
#include "../i18n.h"
namespace ui { namespace screens {
using namespace theme;
void SettingsForm::detach(lv_obj_t* object) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) detach(lv_obj_get_child(object, i));
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {}
}
SettingsForm::~SettingsForm() { if (_body) { detach(_body); lv_obj_clean(_body); } }
void SettingsForm::adopt(lv_obj_t* body, lv_coord_t width) {
  if (_body) { detach(_body); if (_body == body) lv_obj_clean(_body); }
  _body = body; _width = width;
  if (_body) lv_obj_add_event_cb(_body, deleted, LV_EVENT_DELETE, this);
}
void SettingsForm::deleted(lv_event_t* event) {
  auto* self = static_cast<SettingsForm*>(lv_event_get_user_data(event));
  if (self->_body == lv_event_get_target(event)) self->_body = nullptr;
}
bool SettingsForm::accepts(lv_event_t* event) const {
  if (!_body || (_host.deleting && _host.deleting(event))) return false;
  for (auto* p = lv_event_get_target(event); p; p = lv_obj_get_parent(p)) if (p == _body) return true;
  return false;
}
int SettingsForm::settingsRowLabel(lv_obj_t* body, int y, int y_off, const char* text,
                            uint32_t color, const lv_font_t* font, int reserve_right) {
  lv_obj_t* l = lv_label_create(body);
  lv_obj_set_width(l, _width - 2 - reserve_right);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, TR(text));
  lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
  // Always set a font. Inheriting looks equivalent but is not: the inherited
  // value is LVGL's raw LV_FONT_MONTSERRAT_14, which carries no fallback chain,
  // so every non-ASCII character in these labels rendered as a tofu box —
  // "Közösségi profil" came out "K□z□ss□gi profil" while the title bar beside it
  // was fine, because that one sets &font16() explicitly. font14() is the same
  // typeface and metrics, plus the accent/Greek/Cyrillic/Arabic fallbacks.
  lv_obj_set_style_text_font(l, font ? font : &font14(), LV_PART_MAIN);
  lv_obj_set_pos(l, 2, y + y_off);
  lv_obj_update_layout(l);
  return lv_obj_get_height(l);
}
} }
