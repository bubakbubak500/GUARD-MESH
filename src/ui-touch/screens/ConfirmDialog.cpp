#include "ConfirmDialog.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <utility>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;

void ConfirmDialog::dismiss() {
  ++_generation;
  _action = nullptr;
  auto *old = _root;
  _root = nullptr;
  if (old) {
    detachCallbacks(old);
    _host.closeRoot(&old);
  }
}
void ConfirmDialog::detachCallbacks(lv_obj_t *object) {
  // The deferred tree may outlive this controller, so none of its callbacks
  // may retain a pointer to us after dismiss (even during a replacement show).
  lv_obj_remove_event_cb_with_user_data(object, cancelEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, acceptEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, deleteEvent, this);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    detachCallbacks(lv_obj_get_child(object, i));
}
bool ConfirmDialog::owns(lv_obj_t *object) const {
  while (object) {
    if (object == _root)
      return true;
    object = lv_obj_get_parent(object);
  }
  return false;
}
void ConfirmDialog::cancelEvent(lv_event_t *event) {
  auto *self = static_cast<ConfirmDialog *>(lv_event_get_user_data(event));
  if (self && self->owns(lv_event_get_target(event)))
    self->dismiss();
}
void ConfirmDialog::acceptEvent(lv_event_t *event) {
  auto *self = static_cast<ConfirmDialog *>(lv_event_get_user_data(event));
  if (!self || !self->owns(lv_event_get_target(event)))
    return;
  CapturedAction action = std::move(self->_action);
  self->dismiss();
  if (action)
    action();
}
void ConfirmDialog::deleteEvent(lv_event_t *event) {
  auto *self = static_cast<ConfirmDialog *>(lv_event_get_user_data(event));
  if (self && lv_event_get_target(event) == self->_root) {
    self->_root = nullptr;
    self->_action = nullptr;
    ++self->_generation;
  }
}
void ConfirmDialog::show(const char *msg, const char *ok_label, Action on_confirm, bool actions_only_nav) {
  showCaptured(msg, ok_label, on_confirm, actions_only_nav);
}
void ConfirmDialog::showCaptured(const char *msg, const char *ok_label, CapturedAction on_confirm,
                                 bool actions_only_nav) {
  const uint32_t request = _generation + 1;
  dismiss();
  if (_generation != request || _root)
    return;
  _action = std::move(on_confirm);

  // Backdrop: semi-opaque full-screen catcher so taps outside the card do nothing.
  _root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(_root, deleteEvent, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(_root);
  // Backdrop starts below the global status bar — full screen so the card
  // centers on-screen and the tap-catcher covers everything. (When this was a
  // fixed 240-wide rect, in landscape the card landed partly off-screen with
  // its buttons unreachable while the catcher still ate every tap — the UI
  // looked "frozen".)
  lv_obj_set_size(_root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr) - _host.contentTop());
  lv_obj_set_pos(_root, 0, _host.contentTop());
  lv_obj_set_style_bg_opa(_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_bg_color(_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_root, LV_OBJ_FLAG_FLOATING);
  lv_obj_move_foreground(_root);

  // Card — the height FOLLOWS the wrapped message. It used to be a fixed PSC(160) with a
  // freely-wrapping label, so a long message (the crash-report prompt is the worst case)
  // simply ran down over the Cancel/OK buttons (#97). Grow to fit the text, keep the old
  // 160 as a floor so every short dialog looks exactly as before, and cap to the screen so
  // the card can never overflow; the message area below scrolls if the cap bit.
  lv_obj_t *card = lv_obj_create(_root);
  lv_obj_remove_style_all(card);
#if CAP_LARGE_SCREEN
  const lv_coord_t cf_lblw = PSC(186 - 32);
#else
  const lv_coord_t cf_lblw = 186 - 32;
#endif
  lv_point_t cf_tsz;
  lv_txt_get_size(&cf_tsz, TR(msg), &font14(), 0, 2, cf_lblw, LV_TEXT_FLAG_NONE);
  const lv_coord_t cf_chrome = (lv_coord_t)(PSC(12) * 2 + PSC(14) + PSC(34)); // pads + gap + buttons
  lv_coord_t cf_h = (lv_coord_t)(cf_tsz.y + cf_chrome);
  if (cf_h < PSC(160))
    cf_h = PSC(160);
  const lv_coord_t cf_max = lv_disp_get_ver_res(nullptr) - _host.contentTop() - 12;
  if (cf_h > cf_max)
    cf_h = cf_max;
  lv_coord_t cf_msgh = (lv_coord_t)(cf_h - cf_chrome);
  if (cf_msgh < PSC(20))
    cf_msgh = PSC(20); // never let a tall UI scale invert this
  lv_obj_set_size(card, PCW(210), cf_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 12);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, PSC(12), LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *close_x = addCloseXBadge(card, cancelEvent, this); // X behaves like Cancel
  if (actions_only_nav)
    lv_obj_add_flag(close_x, LV_OBJ_FLAG_USER_1);

  // Message area: its own box occupying exactly the space ABOVE the buttons, so the text
  // physically cannot reach them; scrolls vertically when the card hit the screen cap.
  // Width is still shortened (cf_lblw) so the first line doesn't slide under the X badge.
  lv_obj_t *msg_box = lv_obj_create(card);
  lv_obj_remove_style_all(msg_box);
  if (actions_only_nav)
    lv_obj_add_flag(msg_box, LV_OBJ_FLAG_USER_1);
  lv_obj_set_size(msg_box, cf_lblw, cf_msgh);
  lv_obj_align(msg_box, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_scroll_dir(msg_box, LV_DIR_VER);
  lv_obj_t *lbl = lv_label_create(msg_box);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, cf_lblw);
  lv_label_set_text(lbl, TR(msg));
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &font14(), LV_PART_MAIN);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *b_cancel = lv_btn_create(card);
  lv_obj_set_size(b_cancel, PSC(80), PSC(34));
  lv_obj_align(b_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  styleButton(b_cancel);
  lv_obj_set_style_bg_color(b_cancel, lv_color_hex(colors().COLOR_SECONDARY_ACTION), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b_cancel, lv_color_hex(themeRole(0x2D3947, colors().COLOR_CONTROL_PRESSED)),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b_cancel, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_add_event_cb(b_cancel, cancelEvent, LV_EVENT_CLICKED, this);
  lv_obj_t *lc = lv_label_create(b_cancel);
  useChainedFont(lc);
  lv_label_set_text(lc, TR("Cancel"));
  uiFitLabelWidth(lc, PSC(80) - 8);
  lv_obj_center(lc);

  lv_obj_t *b_ok = lv_btn_create(card);
  lv_obj_set_size(b_ok, SC(100), SC(34));
  lv_obj_align(b_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  styleButton(b_ok);
  lv_obj_add_event_cb(b_ok, acceptEvent, LV_EVENT_CLICKED, this);
  lv_obj_t *lo = lv_label_create(b_ok);
  useChainedFont(lo);
  lv_label_set_text(lo, ok_label ? TR(ok_label) : "OK");
  uiFitLabelWidth(lo, SC(100) - 8);
  lv_obj_center(lo);
#if CAP_KEYPAD_NAV
  if (actions_only_nav) {
    if (_host.focus)
      _host.focus(b_cancel);
  }
#endif
}

} // namespace screens
} // namespace ui
