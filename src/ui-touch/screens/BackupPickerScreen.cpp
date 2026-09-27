// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackupPickerScreen.h"

#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"

namespace ui {
namespace screens {
using namespace services;
using namespace theme;
using namespace widgets;

BackupPickerScreen::BackupPickerScreen(Host host)
    : _host(host), _catalog(host.catalog), _confirmation(host.confirmation) {}

BackupPickerScreen::~BackupPickerScreen() {
  _destroying = true;
  detach();
}

void BackupPickerScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}

void BackupPickerScreen::detach() {
  ++_generation;
  auto *root = _root.get();
  if (root)
    unbind(root);
  else {
    unbind(_close.get());
    for (auto &row : _rows)
      unbind(row.get());
  }
  _root.set(nullptr);
  _close.set(nullptr);
  for (auto &row : _rows)
    row.set(nullptr);
  _catalog.clear();
  _confirmation.dismiss();
}

void BackupPickerScreen::deleted(lv_event_t *event) {
  // ObjectRef is registered before this observer and clears _root first. The
  // detach path therefore only retires descendants during a root DELETE.
  auto *self = static_cast<BackupPickerScreen *>(lv_event_get_user_data(event));
  // A close callback may synchronously open a replacement before LVGL runs the
  // old root's deferred DELETE event. ObjectRef leaves the replacement root
  // intact, so that stale DELETE must not detach the replacement page.
  if (self && !self->_root.get())
    self->detach();
}

bool BackupPickerScreen::owns(lv_event_t *event) const {
  auto *object = lv_event_get_target(event);
  while (object) {
    if (object == _root.get())
      return true;
    object = lv_obj_get_parent(object);
  }
  return false;
}

void BackupPickerScreen::close() {
  ++_generation;
  auto *old = _root.get();
  if (old)
    unbind(old);
  else {
    unbind(_close.get());
    for (auto &row : _rows)
      unbind(row.get());
  }
  _root.set(nullptr);
  _close.set(nullptr);
  for (auto &row : _rows)
    row.set(nullptr);
  _catalog.clear();
  _confirmation.dismiss();
  if (old && _host.confirmation.closeRoot)
    _host.confirmation.closeRoot(&old);
}

void BackupPickerScreen::pick(lv_event_t *event) {
  if (!_root.get() || !owns(event) || lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  auto *target = lv_event_get_target(event);
  if (target == _close.get()) {
    close();
    return;
  }
  std::size_t index = BackupCatalog::MaxEntries;
  for (std::size_t i = 0; i < BackupCatalog::MaxEntries; ++i)
    if (target == _rows[i].get()) {
      index = i;
      break;
    }
  BackupPath selected;
  if (index >= _catalog.count() || !_catalog.copyPath(index, selected))
    return;
  const auto import = _host.importBackup;
  void *const context = _host.context;
  const uint32_t expected = _generation + 1;
  close();
  if (_destroying || _generation != expected || _root.get())
    return;
  _confirmation.showCaptured(
      TR("Import this backup?\nReplaces identity,\nchannels & contacts,\nthen reboots."), TR("Import"),
      [this, generation = _generation, selected, import, context] {
        if (_destroying || _generation != generation)
          return;
        if (import)
          import(context, selected);
      },
      false);
}

void BackupPickerScreen::event(lv_event_t *event) {
  auto *self = static_cast<BackupPickerScreen *>(lv_event_get_user_data(event));
  if (self)
    self->pick(event);
}

void BackupPickerScreen::open() {
  if (_destroying)
    return;
  const uint32_t expected = _generation + 1;
  close();
  if (_generation != expected || _root.get() || _confirmation.isOpen())
    return;
  _catalog.scan();
  if (_generation != expected || _root.get() || _confirmation.isOpen())
    return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  auto *root = lv_obj_create(lv_layer_top());
  if (!_root.set(root)) {
    lv_obj_del(root);
    return;
  }
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - (_host.confirmation.contentTop ? _host.confirmation.contentTop() : 0));
  lv_obj_set_pos(root, 0, _host.confirmation.contentTop ? _host.confirmation.contentTop() : 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  auto *title = lv_label_create(root);
  lv_label_set_text(title, TR("Import settings"));
  lv_obj_set_style_text_font(title, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 8);

  auto *closeButton = lv_btn_create(root);
  _close.set(closeButton);
  lv_obj_set_size(closeButton, 30, 26);
  lv_obj_align(closeButton, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(closeButton);
  lv_obj_add_event_cb(closeButton, event, LV_EVENT_CLICKED, this);
  auto *closeLabel = lv_label_create(closeButton);
  lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
  tanCloseRed(closeLabel);
  lv_obj_set_style_text_font(closeLabel, &font12(), LV_PART_MAIN);
  lv_obj_center(closeLabel);

  auto *list = lv_obj_create(root);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, sw - 12, sh - (_host.confirmation.contentTop ? _host.confirmation.contentTop() : 0) - 42);
  lv_obj_set_pos(list, 6, 36);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  if (_catalog.count() == 0) {
    auto *empty = lv_label_create(list);
    lv_label_set_text(empty,
                      TR("No .json backups found.\nExport one first, or copy a\nmeshcore-backup.json to the\nSD card or internal flash."));
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &font12(), LV_PART_MAIN);
  }
  for (std::size_t i = 0; i < BackupCatalog::MaxEntries && i < _catalog.count(); ++i) {
    auto *button = lv_btn_create(list);
    _rows[i].set(button);
    lv_obj_set_width(button, lv_pct(100));
    lv_obj_set_height(button, 36);
    styleButton(button);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    auto *label = lv_label_create(button);
    useChainedFont(label);
    lv_label_set_text(label, _catalog.display(i));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, lv_pct(94));
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 4, 0);
  }
}

} // namespace screens
} // namespace ui
