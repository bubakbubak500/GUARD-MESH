// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContactActionSheet.h"

#include "../device_caps.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"

#include <cstring>

namespace ui {
namespace screens {

using namespace theme;
using namespace widgets;

ContactActionSheet::Snapshot::Snapshot()
    : isRepeater(false), isRoom(false), fromMap(false), hasMap(false),
      hasSightline(false), canShareLocation(false), favorite(false),
      locationShared(false), blocked(false) {
  memset(publicKey, 0, sizeof(publicKey));
  memset(displayName, 0, sizeof(displayName));
}

ContactActionSheet::Host::Host()
    : context(nullptr), contentTop(nullptr), closeRoot(nullptr), dispatch(nullptr) {}

ContactActionSheet::ContactActionSheet(const Host &host)
    : _host(host), _snapshot(), _generation(0), _destroying(false), _rowCount(0) {
  for (uint8_t i = 0; i < sizeof(_rowContexts) / sizeof(_rowContexts[0]); ++i) {
    _rowContexts[i].owner = this;
    _rowContexts[i].action = Action::Message;
  }
}

ContactActionSheet::~ContactActionSheet() {
  _destroying = true;
  close();
}

bool ContactActionSheet::isOpen() const { return _root.get() != nullptr; }

void ContactActionSheet::unbind(lv_obj_t *object) {
  if (!object)
    return;
  lv_obj_remove_event_cb_with_user_data(object, closeEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, deleted, this);
  for (uint8_t i = 0; i < sizeof(_rowContexts) / sizeof(_rowContexts[0]); ++i)
    lv_obj_remove_event_cb_with_user_data(object, event, &_rowContexts[i]);
  const uint32_t children = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < children; ++i)
    unbind(lv_obj_get_child(object, i));
}

void ContactActionSheet::close() {
  ++_generation;
  lv_obj_t *old = _root.get();
  if (old)
    unbind(old);
  else {
    unbind(_close.get());
    for (uint8_t i = 0; i < sizeof(_rows) / sizeof(_rows[0]); ++i)
      unbind(_rows[i].get());
  }

  _root.set(nullptr);
  _close.set(nullptr);
  for (uint8_t i = 0; i < sizeof(_rows) / sizeof(_rows[0]); ++i)
    _rows[i].set(nullptr);
  _rowCount = 0;
  _snapshot = Snapshot();

  if (old) {
    if (_host.closeRoot)
      _host.closeRoot(_host.context, &old);
    else {
      lv_indev_t *active = lv_indev_get_act();
      if (active)
        lv_indev_wait_release(active);
      lv_obj_del_async(old);
      old = nullptr;
    }
  }
}

void ContactActionSheet::deleted(lv_event_t *event) {
  ContactActionSheet *self =
      static_cast<ContactActionSheet *>(lv_event_get_user_data(event));
  if (!self || self->_destroying)
    return;
  // ObjectRef is registered before this observer and clears _root first. A
  // stale root DELETE therefore cannot detach a newer replacement root.
  if (self->_root.get())
    return;
  // The root ObjectRef has already nulled _root. close() now only detaches
  // descendant callbacks/references; it cannot close this already-deleting
  // root and therefore cannot affect a replacement tree.
  self->close();
}

bool ContactActionSheet::owns(lv_event_t *event) const {
  lv_obj_t *object = lv_event_get_current_target(event);
  lv_obj_t *root = _root.get();
  while (object) {
    if (object == root)
      return true;
    object = lv_obj_get_parent(object);
  }
  return false;
}

void ContactActionSheet::closeEvent(lv_event_t *event) {
  ContactActionSheet *self =
      static_cast<ContactActionSheet *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || lv_event_get_code(event) != LV_EVENT_CLICKED ||
      !self->owns(event))
    return;
  lv_indev_t *active = lv_indev_get_act();
  if (active)
    lv_indev_wait_release(active);
  self->close();
}

void ContactActionSheet::event(lv_event_t *event) {
  RowContext *row = static_cast<RowContext *>(lv_event_get_user_data(event));
  if (!row || !row->owner || row->owner->_destroying ||
      lv_event_get_code(event) != LV_EVENT_CLICKED ||
      !row->owner->owns(event))
    return;
  row->owner->dispatch(event, row->action);
}

void ContactActionSheet::dispatch(lv_event_t *event, Action action) {
  if (!event || _destroying || !_root.get() || !owns(event))
    return;

  if (action == Action::Message || action == Action::Sightline) {
    lv_indev_t *active = lv_indev_get_act();
    if (active)
      lv_indev_wait_release(active);
  }

  const Action copiedAction = action;
  uint8_t publicKey[32];
  memcpy(publicKey, _snapshot.publicKey, sizeof(publicKey));
  void *const context = _host.context;
  void (*const callback)(void *, Action, const uint8_t[32]) = _host.dispatch;
  const uint32_t expected = _generation + 1;

  // Retire every old callback and reference before the host can delete the
  // tree or synchronously replace it. The generation check below prevents the
  // old click from being delivered into such a replacement.
  close();
  if (_destroying || _generation != expected || _root.get() || !callback)
    return;
  callback(context, copiedAction, publicKey);
}

void ContactActionSheet::open(const Snapshot &snapshot) {
  if (_destroying)
    return;
  const uint32_t expected = _generation + 1;
  close();
  if (_destroying || _generation != expected || _root.get())
    return;

  _snapshot = snapshot;
  _snapshot.displayName[sizeof(_snapshot.displayName) - 1] = '\0';
  build(_snapshot);
}

void ContactActionSheet::build(const Snapshot &snapshot) {
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  const lv_coord_t top = _host.contentTop ? _host.contentTop() : 0;

  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!_root.set(root)) {
    lv_obj_del(root);
    return;
  }
  // ObjectRef is deliberately registered before this observer. It clears the
  // owner reference first during DELETE, so deleted() can safely clean all
  // descendant references and preserve later observers.
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - top);
  lv_obj_set_pos(root, 0, top);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(root);
  lv_obj_add_event_cb(root, closeEvent, LV_EVENT_CLICKED, this);

#if defined(TLORA_PAGER)
  const int card_w = sw - 80;
  const int btn_h = 26;
  const int btn_gap = 3;
  const int title_h = 32;
  const int padding = 6;
#elif CAP_LARGE_SCREEN
  const int card_w = PCW(232);
  const int btn_h = PSC(30);
  const int btn_gap = PSC(6);
  const int title_h = PSC(28);
  const int padding = PSC(6);
#else
  const int card_w = 232;
  const int btn_h = 30;
  const int btn_gap = 6;
  const int title_h = 28;
  const int padding = 6;
#endif

  const int grid_items = (snapshot.fromMap ? 5 : 7) +
                         (snapshot.isRepeater ? 2 : 0) +
                         (snapshot.isRoom ? 1 : 0) +
                         (snapshot.hasSightline ? 1 : 0) +
                         (snapshot.hasMap ? 1 : 0);
  const int grid_rows = (grid_items + 1) / 2;
  const int body_content_h = (grid_rows + 1) * btn_h + grid_rows * btn_gap;
  int card_h = 2 * padding + title_h + body_content_h;
  const int avail_card_h = sh - top - 4;
  if (card_h > avail_card_h)
    card_h = avail_card_h;

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, padding, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *closeButton = addCloseXBadge(card, closeEvent, this);
  _close.set(closeButton);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text_fmt(title, "%s%s",
                        snapshot.isRepeater ? TOUCH_SYM_ANTENNA "  " :
                        snapshot.isRoom ? LV_SYMBOL_LOOP "  " : TOUCH_SYM_PERSON "  ",
                        snapshot.displayName[0] ? snapshot.displayName : "(unnamed)");
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 2 * padding - 32);
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t *body = lv_obj_create(card);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 0, title_h);
  lv_obj_set_size(body, card_w - 2 * padding, card_h - 2 * padding - title_h);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  if (body_content_h > card_h - 2 * padding - title_h)
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
  else
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

#if defined(TLORA_PAGER)
  const int col_gap = btn_gap;
#elif CAP_LARGE_SCREEN
  const int col_gap = PSC(6);
#else
  const int col_gap = 6;
#endif
  const int half_w = (card_w - 2 * padding - col_gap) / 2;
  int y = 0;
  int col = 0;
  const lv_font_t *row_font =
#if defined(TLORA_PAGER)
      uiChromeFont();
#elif CAP_LARGE_SCREEN
      &font14();
#else
      &font12();
#endif

  auto addButton = [&](const char *label, Action action, uint32_t bg) {
    if (_rowCount >= sizeof(_rows) / sizeof(_rows[0]))
      return;
    lv_obj_t *button = lv_btn_create(body);
    lv_obj_set_size(button, half_w, btn_h);
    lv_obj_set_pos(button, col == 0 ? 0 : half_w + col_gap, y);
    styleButton(button);
    lv_obj_set_style_pad_ver(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(button, 4, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_bg_color(button, lv_color_hex(bg), LV_PART_MAIN);
    _rowContexts[_rowCount].owner = this;
    _rowContexts[_rowCount].action = action;
    _rows[_rowCount].set(button);
    ++_rowCount;
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED,
                        &_rowContexts[_rowCount - 1]);
    lv_obj_t *labelObject = lv_label_create(button);
    lv_label_set_text(labelObject, TR(label));
    lv_obj_set_style_text_font(labelObject, row_font, LV_PART_MAIN);
    lv_label_set_long_mode(labelObject, LV_LABEL_LONG_DOT);
    lv_obj_set_size(labelObject, half_w - 8, lv_font_get_line_height(row_font));
    lv_obj_set_style_text_align(labelObject, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(labelObject);
    if (col == 0)
      col = 1;
    else {
      col = 0;
      y += btn_h + btn_gap;
    }
  };

  auto addFullButton = [&](const char *label, Action action, uint32_t bg) {
    if (_rowCount >= sizeof(_rows) / sizeof(_rows[0]))
      return;
    if (col == 1) {
      col = 0;
      y += btn_h + btn_gap;
    }
    lv_obj_t *button = lv_btn_create(body);
    lv_obj_set_size(button, card_w - 2 * padding, btn_h);
    lv_obj_set_pos(button, 0, y);
    styleButton(button);
    lv_obj_set_style_pad_ver(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(button, 8, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_bg_color(button,
                                lv_color_hex(themeRole(bg, colors().COLOR_STATUS_DANGER)),
                                LV_PART_MAIN);
    _rowContexts[_rowCount].owner = this;
    _rowContexts[_rowCount].action = action;
    _rows[_rowCount].set(button);
    ++_rowCount;
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED,
                        &_rowContexts[_rowCount - 1]);
    lv_obj_t *labelObject = lv_label_create(button);
    lv_label_set_text(labelObject, TR(label));
    lv_obj_set_style_text_font(labelObject, row_font, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_text_color(labelObject,
                                  lv_color_hex(colors().COLOR_ON_STATUS_DANGER),
                                  LV_PART_MAIN);
    lv_obj_center(labelObject);
    y += btn_h + btn_gap;
  };

  if (snapshot.isRoom) {
    addButton(TR(LV_SYMBOL_LOOP "  Join"), Action::Join, colors().COLOR_STATUS_OK);
    addButton(TR(LV_SYMBOL_ENVELOPE "  Open chat"), Action::Message, 0);
  } else if (snapshot.isRepeater) {
    addButton(TR(LV_SYMBOL_REFRESH "  Ping"), Action::Ping, 0);
  } else {
    addButton(TR(LV_SYMBOL_ENVELOPE "  Message"), Action::Message, 0);
  }
  addButton(TR(LV_SYMBOL_BATTERY_3 "  Telemetry"), Action::Telemetry, 0);
  if (snapshot.hasMap)
    addButton(TR(LV_SYMBOL_GPS "  Show on map"), Action::ShowOnMap, 0);
  if (snapshot.isRepeater) {
    addButton(TR(LV_SYMBOL_GPS "  Trace SNR"), Action::Trace, 0);
    addButton(TR(LV_SYMBOL_SETTINGS "  Admin"), Action::Admin, 0);
  }
  addButton(TR(LV_SYMBOL_WIFI "  Range test"), Action::RangeTest, 0);
  if (snapshot.hasSightline)
    addButton(TR(LV_SYMBOL_GPS "  Sightline"), Action::Sightline, 0);
  addButton(snapshot.favorite ? TOUCH_SYM_STAR "  Unfav" : TOUCH_SYM_STAR "  Favorite",
            Action::Favorite, 0);
  if (!snapshot.fromMap && snapshot.canShareLocation)
    addButton(snapshot.locationShared ? TR(LV_SYMBOL_GPS "  Stop sharing loc")
                                      : TR(LV_SYMBOL_GPS "  Share my loc"),
              Action::ShareLocation, 0);
  addButton(TR(LV_SYMBOL_LOOP "  Reset path"), Action::ResetPath, 0);
  if (!snapshot.fromMap)
    addButton(TR(LV_SYMBOL_UPLOAD "  Share contact"), Action::ShareContact, 0);
  if (!snapshot.fromMap)
    addButton(snapshot.blocked ? LV_SYMBOL_OK "  Unblock" : LV_SYMBOL_CLOSE "  Block",
              Action::Block, 0);
  addFullButton(LV_SYMBOL_TRASH "  Delete", Action::Delete, 0xB23A48);

  const int body_h = card_h - 2 * padding - title_h;
  if (y > body_h) {
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  }
}

} // namespace screens
} // namespace ui
