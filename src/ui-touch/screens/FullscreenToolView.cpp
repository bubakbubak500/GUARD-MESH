// SPDX-License-Identifier: GPL-3.0-or-later
#include "FullscreenToolView.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include <cstring>
namespace ui { namespace screens {
using namespace ui::theme;
using namespace ui::widgets;
lv_obj_t* FullscreenToolView::open(const char* title) {
  close();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  _root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(_root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(_root);
  lv_obj_set_size(_root, sw, sh - _host.statusHeight());   // keep the status bar visible
  lv_obj_set_pos(_root, 0, _host.statusHeight());
  lv_obj_set_style_bg_color(_root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(_root);

  // Show the view name in the status bar's left zone (where MESHCOMOD sits)
  // rather than a dedicated title row — that reclaims the vertical space.
  strncpy(_title, title ? title : "", sizeof(_title) - 1);
  _title[sizeof(_title) - 1] = '\0';
  _host.changed();

  // Body fills the whole view (no header row).
  lv_obj_t* body = lv_obj_create(_root);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, sw, sh - _host.statusHeight());
  lv_obj_set_pos(body, 0, 0);
  lv_obj_set_style_pad_all(body, 6, LV_PART_MAIN);

  // Home button floats as a small overlay over the top-right of the body.
  lv_obj_t* home = lv_btn_create(_root);
  lv_obj_set_size(home, 40, 28);
  lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(home);
#if !defined(HAS_TANMATSU)
  if (isDay()) {
    lv_obj_set_style_bg_color(home, lv_color_hex(colors().COLOR_ACCENT_PRESS), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(home, lv_color_hex(colors().COLOR_ON_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_opa(home, LV_OPA_COVER, LV_PART_MAIN);
  }
#endif
  lv_obj_add_event_cb(home, [](lv_event_t* event) {
    auto& self = *static_cast<FullscreenToolView*>(lv_event_get_user_data(event));
    if (lv_obj_get_parent(lv_event_get_target(event)) != self._root) return;
    self.close();
    self._host.home();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* hl = lv_label_create(home);
  useChainedFont(hl);
#if defined(HAS_TANMATSU)
  lv_label_set_text(hl, LV_SYMBOL_CLOSE);   // red ✕ close
  lv_obj_set_style_text_color(hl, lv_color_hex(0xE05544), LV_PART_MAIN);
#else
  lv_label_set_text(hl, LV_SYMBOL_HOME);
  if (isDay()) lv_obj_set_style_text_font(hl, &font16(), LV_PART_MAIN);
#endif
  lv_obj_center(hl);
  lv_obj_move_foreground(home);
  return body;
}
void FullscreenToolView::detach(lv_obj_t* object) {
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {}
  for (uint32_t i=0; i<lv_obj_get_child_cnt(object); ++i) detach(lv_obj_get_child(object,i));
}
void FullscreenToolView::close() {
  if (!_root) return;
  auto* old = _root;
  _root = nullptr;
  _title[0] = 0;
  _host.releaseContents();
  detach(old);
  _host.closePopup(&old);
  _host.changed();
}
void FullscreenToolView::deleted(lv_event_t* event) {
  auto& self = *static_cast<FullscreenToolView*>(lv_event_get_user_data(event));
  if (lv_event_get_target(event) != self._root) return;
  self._root = nullptr;
  self._title[0] = 0;
  self._host.releaseContents();
  self._host.changed();
}
} }
