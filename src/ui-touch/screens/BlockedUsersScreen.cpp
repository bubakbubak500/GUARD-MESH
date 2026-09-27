// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockedUsersScreen.h"

#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <new>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

BlockedUsersScreen::BlockedUsersScreen(Host host) : _host(host) {}
BlockedUsersScreen::~BlockedUsersScreen() {
  _destroying = true;
  close();
}

bool BlockedUsersScreen::still(uint32_t generation, lv_obj_t *root) const {
  return !_destroying && root && _generation == generation && _root.get() == root;
}

void BlockedUsersScreen::detachRows(lv_obj_t *root, RowContext *rows,
                                    unsigned count) {
  if (!root || !rows) return;
  // Remove only callbacks that use this owner's contexts. The closeRoot Host
  // may defer deleting the old tree, while these contexts are released now.
  for (unsigned i = 0; i < count; ++i)
    lv_obj_remove_event_cb_with_user_data(root, rowClicked, &rows[i]);
  const uint32_t children = lv_obj_get_child_cnt(root);
  for (uint32_t i = 0; i < children; ++i)
    detachRows(lv_obj_get_child(root, i), rows, count);
}

void BlockedUsersScreen::retire(bool deleting, lv_obj_t *deletingRoot) {
  lv_obj_t *oldRoot = deleting ? deletingRoot : _root.get();
  if (!oldRoot && !_rows) return;
  const uint32_t generation = ++_generation;
  widgets::ObjectRef retiredRoot;
  bool watched = false;
  if (oldRoot && !deleting) {
    lv_obj_remove_event_cb_with_user_data(oldRoot, rootDeleted, this);
    watched = retiredRoot.set(oldRoot); // watches synchronous Host deletion/reentry
  }
  RowContext *oldRows = _rows;
  const unsigned oldCount = _rowCount;
  _root.set(nullptr);
  _rows = nullptr;
  _rowCount = 0;
  detachRows(oldRoot, oldRows, oldCount);
  if (oldRows) platform::release(oldRows);

  if (!deleting && oldRoot) {
    // closeRoot may schedule deletion or synchronously delete the captured
    // root. It must never be asked to close a replacement made by reentry.
    if (_host.closeRoot) {
      // If LVGL could not register a temporary watcher, this is still the
      // original object: no Host callback has run since capturing it.
      lv_obj_t *captured = watched ? retiredRoot.get() : oldRoot;
      if (captured) _host.closeRoot(_host.context, &captured);
    } else if (lv_obj_t *captured = watched ? retiredRoot.get() : oldRoot) {
      lv_obj_del(captured);
    }
  }
  if (_generation == generation && _host.endPageIfOwned)
    _host.endPageIfOwned(_host.context);
  if (_generation == generation && _host.navDirty)
    _host.navDirty(_host.context);
}

void BlockedUsersScreen::close() { retire(false); }

void BlockedUsersScreen::rootDeleted(lv_event_t *event) {
  auto *self = static_cast<BlockedUsersScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
  // Explicit retirement removes this callback from the old root. Thus any
  // DELETE reaching us belongs to the current page, including an empty one.
  self->retire(true, lv_event_get_target(event));
}

void BlockedUsersScreen::rowClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *row = static_cast<RowContext *>(lv_event_get_user_data(event));
  if (!row || !row->owner) return;
  BlockedUsersScreen *self = row->owner;
  lv_obj_t *root = self->_root.get();
  if (!self->still(row->generation, root)) return;
  const uint32_t generation = row->generation;
  const Kind kind = row->kind;
  uint8_t key[KeyBytes];
  char name[NameBytes];
  std::memcpy(key, row->key, sizeof key);
  std::memcpy(name, row->name, sizeof name);
  name[NameBytes - 1] = 0;
  if (kind == Kind::Key) {
    if (self->_host.unblockKey)
      self->_host.unblockKey(self->_host.context, key);
  } else if (name[0] && self->_host.unblockName) {
    self->_host.unblockName(self->_host.context, name);
  }
  if (self->still(generation, root)) self->rebuild();
}

void BlockedUsersScreen::appendRow(lv_obj_t *list, const char *label,
                                   RowContext &context) {
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_obj_t *row = lv_obj_create(list);
  if (!row) return;
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, sw - 16, 40);
  lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *text = lv_label_create(row);
  lv_label_set_text(text, label);
  lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
  lv_obj_set_width(text, sw - 16 - 96);
  lv_obj_set_style_text_color(text, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(text, &font14(), LV_PART_MAIN);
  lv_obj_align(text, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t *button = lv_btn_create(row);
  lv_obj_set_size(button, 80, 30);
  lv_obj_align(button, LV_ALIGN_RIGHT_MID, -6, 0);
  styleButton(button);
  lv_obj_add_event_cb(button, rowClicked, LV_EVENT_CLICKED, &context);
  lv_obj_t *caption = lv_label_create(button);
  lv_label_set_text(caption, TR("Unblock"));
  lv_obj_set_style_text_font(caption, &font12(), LV_PART_MAIN);
  lv_obj_center(caption);
}

void BlockedUsersScreen::fillRows(lv_obj_t *root, uint32_t generation,
                                  lv_coord_t top) {
  Snapshot snapshot;
  const bool loaded = _host.readSnapshot &&
                      _host.readSnapshot(_host.context, snapshot);
  if (!still(generation, root)) return;
  if (!loaded) {
    lv_obj_t *error = lv_label_create(root);
    lv_label_set_text(error, TR("Could not load blocked users"));
    lv_obj_set_style_text_color(error, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(error, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(error, 8, 8);
    return;
  }
  const unsigned keys = snapshot.keyCount < KeyCapacity
                          ? snapshot.keyCount : KeyCapacity;
  const unsigned names = snapshot.nameCount < NameCapacity
                           ? snapshot.nameCount : NameCapacity;
  unsigned validNames = 0;
  for (unsigned i = 0; i < names; ++i) {
    snapshot.names[i].name[NameBytes - 1] = 0;
    if (snapshot.names[i].name[0]) ++validNames;
  }
  if (!keys && !validNames) {
    lv_obj_t *empty = lv_label_create(root);
    lv_label_set_text(empty,
      TR("No blocked users.\n\nLong-press a message and tap\nBlock to add one."));
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(empty, 8, 8);
    return;
  }

  RowContext *rows = static_cast<RowContext *>(
      platform::allocate(sizeof(RowContext) * RowCapacity, true));
  if (!rows) rows = static_cast<RowContext *>(
      platform::allocate(sizeof(RowContext) * RowCapacity, false));
  if (!rows) {
    lv_obj_t *error = lv_label_create(root);
    lv_label_set_text(error, TR("Out of memory"));
    lv_obj_set_pos(error, 8, 8);
    return;
  }
  _rows = rows;
  _rowCount = 0;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  lv_obj_t *list = lv_obj_create(root);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, sw - 12, sh - top - 16);
  lv_obj_set_pos(list, 6, 8);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  for (unsigned i = 0; i < keys; ++i) {
    RowContext &row = rows[_rowCount++];
    new (&row) RowContext{};
    row.owner = this; row.generation = generation; row.kind = Kind::Key;
    std::memcpy(row.key, snapshot.keys[i].key, KeyBytes);
    row.name[0] = 0;
    snapshot.keys[i].display[sizeof snapshot.keys[i].display - 1] = 0;
    char hex[13];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X%02X%02X%02X",
      row.key[0], row.key[1], row.key[2], row.key[3], row.key[4], row.key[5]);
    appendRow(list, snapshot.keys[i].display[0] ? snapshot.keys[i].display : hex,
              row);
  }
  for (unsigned i = 0; i < names; ++i) {
    if (!snapshot.names[i].name[0]) continue;
    RowContext &row = rows[_rowCount++];
    new (&row) RowContext{};
    row.owner = this; row.generation = generation; row.kind = Kind::Name;
    std::memset(row.key, 0, sizeof row.key);
    std::memcpy(row.name, snapshot.names[i].name, NameBytes);
    appendRow(list, row.name, row);
  }
}

void BlockedUsersScreen::show() {
  if (_destroying) return;
  if (_root.get()) close();
  if (_destroying || _root.get()) return; // closeRoot may have opened a replacement
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!root) return;
  if (!_root.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, this);
  const uint32_t generation = ++_generation;
  lv_obj_remove_style_all(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  const int measuredTop = _host.beginPage
                        ? _host.beginPage(_host.context, "Blocked users") : 0;
  if (!still(generation, root)) return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  const lv_coord_t top = measuredTop < 0 ? 0
                         : measuredTop > sh ? sh : measuredTop;
  lv_obj_set_size(root, sw, sh - top);
  lv_obj_set_pos(root, 0, top);
  lv_obj_move_foreground(root);
  if (_host.bringStatusBarFront) _host.bringStatusBarFront(_host.context);
  if (!still(generation, root)) return;
  fillRows(root, generation, top);
  if (still(generation, root) && _host.navDirty)
    _host.navDirty(_host.context);
}

void BlockedUsersScreen::rebuild() {
  if (_root.get()) show();
}

} } // namespace ui::screens
