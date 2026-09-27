// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackupSettingsScreen.h"

#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>

namespace ui {
namespace screens {
using namespace services;
using namespace theme;
using namespace widgets;

struct BackupSettingsScreen::Deferred {
  BackupSettingsScreen *owner = nullptr;
  uint32_t generation = 0;
};

BackupSettingsScreen::BackupSettingsScreen(Host host)
    : _host(host), _catalog(host.catalog), _confirmation(host.confirmation) {}

BackupSettingsScreen::~BackupSettingsScreen() {
  _destroying = true;
  detach();
}

void BackupSettingsScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}

void BackupSettingsScreen::cancelDeferred() {
  auto *pending = _deferred;
  if (!pending)
    return;
  _deferred = nullptr;
  pending->owner = nullptr;
  if (lv_async_call_cancel(deferred, pending) == LV_RES_OK)
    platform::release(pending);
}

void BackupSettingsScreen::detach() {
  ++_generation;
  ++_request;
  cancelDeferred();
  auto *body = _body.get();
  if (body)
    unbind(body);
  else {
    unbind(_export.get());
    unbind(_import.get());
    unbind(_factoryReset.get());
    for (auto &button : _deleteButtons)
      unbind(button.get());
  }
  _body.set(nullptr);
  _export.set(nullptr);
  _import.set(nullptr);
  _factoryReset.set(nullptr);
  for (auto &button : _deleteButtons)
    button.set(nullptr);
  _catalog.clear();
  _confirmation.dismiss();
}

void BackupSettingsScreen::deleted(lv_event_t *event) {
  // ObjectRef is registered before this observer and clears _body before this
  // callback runs. Do not remove the currently deleting root's descriptors.
  auto *self = static_cast<BackupSettingsScreen *>(lv_event_get_user_data(event));
  // A host callback can build a replacement body while the old body is waiting
  // for deferred deletion. Its stale DELETE event must leave the replacement
  // body and catalog untouched.
  if (self && !self->_body.get())
    self->detach();
}

bool BackupSettingsScreen::owns(lv_event_t *event) const {
  auto *object = lv_event_get_target(event);
  while (object) {
    if (object == _body.get())
      return true;
    object = lv_obj_get_parent(object);
  }
  return false;
}

void BackupSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}

void BackupSettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  if (_destroying)
    return;
  const bool reuse = body && body == _body.get();
  const uint32_t expected = _generation + 1;
  detach();
  if (!body || _generation != expected)
    return;
  if (reuse)
    lv_obj_clean(body);
  if (!_body.set(body))
    return;
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  _width = width;
  _catalog.scan();
  if (_generation != expected || !_body.get())
    return;
  render();
}

void BackupSettingsScreen::render() {
  auto *body = _body.get();
  if (!body)
    return;
  const lv_coord_t width = _width;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(6), LV_PART_MAIN);
  lv_obj_set_style_pad_top(body, 0, LV_PART_MAIN);

  auto addAction = [&](const char *symbol, const char *text, widgets::ObjectRef &reference) {
    auto *button = lv_btn_create(body);
    reference.set(button);
    lv_obj_set_size(button, width, SC(40));
    styleButton(button);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    auto *label = lv_label_create(button);
    char caption[96];
    std::snprintf(caption, sizeof(caption), "%s  %s", symbol, text);
    lv_label_set_text(label, caption);
    lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(label);
  };
  addAction(LV_SYMBOL_SAVE, TR("Export new backup"), _export);
  if (_export.get()) {
    lv_obj_set_style_bg_color(_export.get(), lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_export.get(), lv_color_hex(colors().COLOR_STATUS_OK_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    auto *label = lv_obj_get_child(_export.get(), 0);
    lv_obj_set_style_text_color(_export.get(), lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
    if (label)
      lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  }
  if (_host.showImport)
    addAction(LV_SYMBOL_DOWNLOAD, TR("Import backup"), _import);

  auto *heading = lv_label_create(body);
  lv_label_set_text(heading, TR("Saved backups"));
  lv_obj_set_style_text_color(heading, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(heading, &font12(), LV_PART_MAIN);

  if (_catalog.count() == 0) {
    auto *empty = lv_label_create(body);
    lv_label_set_text(empty, TR("No backups yet. Tap Export new backup above to create one."));
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, width);
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &font12(), LV_PART_MAIN);
  }
  for (std::size_t i = 0; i < _catalog.count() && i < BackupCatalog::MaxEntries; ++i) {
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width, SC(38));
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto *name = lv_label_create(row);
    lv_label_set_text(name, _catalog.display(i));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, width - SC(48));
    lv_obj_set_style_text_color(name, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(name, &font12(), LV_PART_MAIN);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 2, 0);

    auto *button = lv_btn_create(row);
    _deleteButtons[i].set(button);
    lv_obj_set_size(button, SC(40), SC(32));
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
    styleButton(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(themeRole(0x7A2A2A, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(themeRole(0x5E2020, colors().COLOR_STATUS_DANGER_PRESSED)),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    auto *deleteLabel = lv_label_create(button);
    lv_label_set_text(deleteLabel, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(deleteLabel, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(deleteLabel,
                                lv_color_hex(themeRole(0xFFD8D8, colors().COLOR_ON_STATUS_DANGER)), LV_PART_MAIN);
    lv_obj_center(deleteLabel);
  }

  auto *dangerHeading = lv_label_create(body);
  lv_label_set_text(dangerHeading, TR("Danger zone"));
  lv_obj_set_style_text_color(dangerHeading, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(dangerHeading, &font12(), LV_PART_MAIN);
  auto *reset = lv_btn_create(body);
  _factoryReset.set(reset);
  lv_obj_set_size(reset, width, SC(40));
  styleButton(reset);
  lv_obj_set_style_bg_color(reset, lv_color_hex(themeRole(0x8B1E1E, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
  lv_obj_set_style_bg_color(reset, lv_color_hex(themeRole(0x6A1616, colors().COLOR_STATUS_DANGER_PRESSED)),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(reset, event, LV_EVENT_CLICKED, this);
  auto *resetLabel = lv_label_create(reset);
  char resetText[64];
  std::snprintf(resetText, sizeof(resetText), "%s  %s", LV_SYMBOL_TRASH, TR("Factory reset"));
  lv_label_set_text(resetLabel, resetText);
  lv_obj_set_style_text_font(resetLabel, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(resetLabel,
                              lv_color_hex(themeRole(0xFFE2E2, colors().COLOR_ON_STATUS_DANGER)), LV_PART_MAIN);
  lv_obj_center(resetLabel);
  auto *explanation = lv_label_create(body);
  lv_label_set_text(explanation,
                    TR("Erases identity, contacts, channels, messages, Wi-Fi and all settings, then reboots."));
  lv_label_set_long_mode(explanation, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(explanation, width);
  lv_obj_set_style_text_color(explanation, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(explanation, &font12(), LV_PART_MAIN);
}

void BackupSettingsScreen::exportBackup() {
  if (!_body.get() || !_host.exportBackup)
    return;
  const uint32_t generation = _generation;
  const auto makeFilename = _host.makeFilename;
  const auto exportBackup = _host.exportBackup;
  void *const context = _host.context;
  char filename[48];
  if (makeFilename) {
    makeFilename(context, filename, sizeof(filename));
    if (_destroying || _generation != generation || !_body.get())
      return;
  }
  else
    std::snprintf(filename, sizeof(filename), "meshcore-backup-%lu.json",
                  static_cast<unsigned long>(platform::milliseconds() / 1000));
  if (_destroying || _generation != generation || !_body.get())
    return;
  exportBackup(context, filename);
  if (!_destroying && _generation == generation && _body.get())
    scheduleRebuild();
}

void BackupSettingsScreen::deleteBackup(lv_event_t *event) {
  if (!_body.get() || !owns(event) || lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  auto *target = lv_event_get_target(event);
  std::size_t index = BackupCatalog::MaxEntries;
  for (std::size_t i = 0; i < BackupCatalog::MaxEntries; ++i)
    if (target == _deleteButtons[i].get()) {
      index = i;
      break;
    }
  if (index >= _catalog.count())
    return;
  BackupPath selected;
  if (!_catalog.copyPath(index, selected))
    return;
  const uint32_t generation = _generation;
  _confirmation.dismiss();
  if (_destroying || _generation != generation || !_body.get())
    return;
  const uint32_t request = ++_request;
  const auto remove = _host.deleteBackup;
  const auto alert = _host.alert;
  void *const context = _host.context;
  _confirmation.showCaptured(
      TR("Delete this backup file?\nThis cannot be undone."), TR("Delete"),
      [this, generation, request, selected, remove, alert, context] {
        if (_destroying || _generation != generation || _request != request || !_body.get())
          return;
        const bool ok = remove ? remove(context, selected) : false;
        if (_generation != generation || !_body.get())
          return;
        if (!ok) {
          if (_destroying || _generation != generation || _request != request || !_body.get())
            return;
          if (alert)
            alert(context, TR("Delete failed"), 1500);
        }
        if (_destroying || _generation != generation || _request != request || !_body.get())
          return;
        scheduleRebuild();
      },
      false);
}

void BackupSettingsScreen::confirmFactoryReset() {
  if (!_body.get())
    return;
  const uint32_t generation = _generation;
  _confirmation.dismiss();
  if (_destroying || _generation != generation || !_body.get())
    return;
  const uint32_t request = ++_request;
  const auto reset = _host.factoryReset;
  void *const context = _host.context;
  _confirmation.showCaptured(
      TR("Factory reset?\n\nErases identity, contacts,\nchannels, messages, Wi-Fi\nand every setting. This\ncannot be undone."),
      TR("Erase all"),
      [this, generation, request, reset, context] {
        if (_destroying || _generation != generation || _request != request || !_body.get())
          return;
        if (reset)
          reset(context);
      },
      false);
}

void BackupSettingsScreen::event(lv_event_t *event) {
  auto *self = static_cast<BackupSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || !self->_body.get() || !self->owns(event))
    return;
  auto *target = lv_event_get_target(event);
  if (target == self->_export.get())
    self->exportBackup();
  else if (target == self->_import.get()) {
    if (self->_host.openPicker)
      self->_host.openPicker(self->_host.context);
  } else if (target == self->_factoryReset.get())
    self->confirmFactoryReset();
  else
    self->deleteBackup(event);
}

void BackupSettingsScreen::scheduleRebuild() {
  if (_destroying || !_body.get())
    return;
  cancelDeferred();
  auto *pending = static_cast<Deferred *>(platform::allocate(sizeof(Deferred), true));
  if (!pending)
    pending = static_cast<Deferred *>(platform::allocate(sizeof(Deferred), false));
  if (!pending)
    return;
  pending->owner = this;
  pending->generation = _generation;
  _deferred = pending;
  if (lv_async_call(deferred, pending) != LV_RES_OK) {
    if (_deferred == pending)
      _deferred = nullptr;
    pending->owner = nullptr;
    platform::release(pending);
  }
}

void BackupSettingsScreen::deferred(void *data) {
  auto *pending = static_cast<Deferred *>(data);
  auto *owner = pending ? pending->owner : nullptr;
  if (owner && owner->_deferred == pending) {
    owner->_deferred = nullptr;
    if (!owner->_destroying && owner->_generation == pending->generation && owner->_body.get())
      owner->build(owner->_body.get(), owner->_width);
  }
  if (pending)
    platform::release(pending);
}

} // namespace screens
} // namespace ui
