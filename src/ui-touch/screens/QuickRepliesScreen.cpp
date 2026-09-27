#include "QuickRepliesScreen.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../i18n.h"
#include <cstdio>
namespace ui { namespace screens {
using namespace theme;
using namespace widgets;
QuickRepliesScreen::~QuickRepliesScreen() {
  if (_body) {
    detachCallbacks(_body);
    lv_obj_clean(_body);
  }
}
void QuickRepliesScreen::detachCallbacks(lv_obj_t* object) {
  lv_obj_remove_event_cb_with_user_data(object, saveEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, deleteEvent, this);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    detachCallbacks(lv_obj_get_child(object, i));
}
bool QuickRepliesScreen::owns(lv_obj_t* object) const {
  while (object) {
    if (object == _body) return true;
    object = lv_obj_get_parent(object);
  }
  return false;
}
void QuickRepliesScreen::deleteEvent(lv_event_t* event) {
  auto* self = static_cast<QuickRepliesScreen*>(lv_event_get_user_data(event));
  if (self && lv_event_get_target(event) == self->_body) {
    self->_body = nullptr;
    for (auto& field : self->_fields) field = nullptr;
  }
}
void QuickRepliesScreen::saveEvent(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  auto* self = static_cast<QuickRepliesScreen*>(lv_event_get_user_data(e));
  if (!self || !self->owns(lv_event_get_target(e))) return;
  self->_host.syncKeyboard();
  int n_saved = 0;
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) {
    lv_obj_t* ta = self->_fields[i];
    if (!ta) continue;
    const char* text = lv_textarea_get_text(ta);
    if (touchPrefsSetQuickReply(i, text ? text : "")) ++n_saved;
  }
  char msg[48];
  snprintf(msg, sizeof(msg), "Saved %d quick %s", n_saved,
           n_saved == 1 ? "reply" : "replies");
  self->_host.alert(msg, 1100);
}

void QuickRepliesScreen::build(lv_obj_t* body) {
  if (_body) detachCallbacks(_body);
  if (_body && _body == body) lv_obj_clean(body);
  _body = body;
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) _fields[i] = nullptr;
  if (!body) return;
  lv_obj_add_event_cb(body, deleteEvent, LV_EVENT_DELETE, this);

  int y = 0;
  // Compact form: one row per slot, with the slot index as a tiny prefix
  // label so the user knows which macro they're editing. Single-line
  // textareas keep the six-slot modal compact; longer replies scroll
  // horizontally while editing.
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) {
    char buf[TOUCH_QUICK_REPLY_MAXLEN];
    touchPrefsGetQuickReply(i, buf, sizeof(buf));

    lv_obj_t* idxlbl = lv_label_create(body);
    char idxs[4];
    snprintf(idxs, sizeof(idxs), "%d", i + 1);
    lv_label_set_text(idxlbl, idxs);
    lv_obj_set_style_text_color(idxlbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(idxlbl, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(idxlbl, 2, y + 9);

    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, SC(200), SC(32));
    lv_obj_set_pos(ta, 20, y);
    styleCard(ta);
    // The 6 fields cover the whole page, so a drag always starts on a textarea.
    // A one-line textarea otherwise grabs the drag for its own (horizontal)
    // scroll and the page never scrolls — clear SCROLLABLE so the drag bubbles
    // up to the scrollable settings page. Tap-to-focus/typing is unaffected.
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, TOUCH_QUICK_REPLY_MAXLEN - 1);
    lv_textarea_set_text(ta, buf);
    lv_obj_set_style_text_color(ta, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &font12(), LV_PART_MAIN);
    // Bind tap -> keyboard mirror exactly like every other settings field.
    // (The old composerFocusCb path is a chat-composer callback that no-ops
    // for a null panel, so QR fields never bound the keyboard and couldn't be
    // typed into; saveQuickRepliesCb already syncs the mirror back on save.)
    _host.attachField(ta);
    _fields[i] = ta;
    y += SC(36);
  }

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),SC(34));
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_add_event_cb(b, saveEvent, LV_EVENT_CLICKED, this);
  lv_obj_t* l = lv_label_create(b);
  useChainedFont(l);
  lv_label_set_text(l, TR("Save quick replies"));
  lv_obj_center(l);
}


} }
