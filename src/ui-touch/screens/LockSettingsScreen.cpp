// SPDX-License-Identifier: GPL-3.0-or-later
#include "LockSettingsScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ColorSwatch.h"
#include "../widgets/Styles.h"
namespace ui {
namespace screens {
using namespace widgets;
using namespace theme;
void LockSettingsScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}
void LockSettingsScreen::detach() {
  ++_generation;
  unbind(_body.get());
  _body.set(nullptr);
  unbind(_wallpaper.get());
  _wallpaper.set(nullptr);
  _caption.set(nullptr);
  unbind(_locked.get());
  _locked.set(nullptr);
  for (auto &object : _colors) {
    unbind(object.get());
    object.set(nullptr);
  }
}
void LockSettingsScreen::deleted(lv_event_t *e) {
  static_cast<LockSettingsScreen *>(lv_event_get_user_data(e))->detach();
}
void LockSettingsScreen::wallpaperChanged(const char *path) {
  if (!_caption.get())
    return;
  char text[64];
  LockSettings::wallpaperName(path, text, sizeof text);
  lv_label_set_text(_caption.get(), text);
}
void LockSettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  const bool reuse = body && body == _body.get();
  detach();
  if (!body)
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(10), LV_PART_MAIN);
  const auto caps = _settings.capabilities();
  const auto state = _settings.read();
  auto label = [&](lv_obj_t *parent, const char *text) {
    auto *object = lv_label_create(parent);
    useChainedFont(object);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_width(object, width - 4);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    return object;
  };
  if (caps.wallpaper) {
    label(body, TR("Lock screen wallpaper"));
    auto *button = lv_btn_create(body);
    _wallpaper.set(button);
    lv_obj_set_size(button, width - 4, SC(34));
    styleButton(button);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    _caption.set(label(button, ""));
    lv_obj_set_width(_caption.get(), lv_pct(92));
    lv_label_set_long_mode(_caption.get(), LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_caption.get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(_caption.get());
    wallpaperChanged(state.wallpaper);
  }
  if (caps.color) {
    label(body, TR("Lock text colour"));
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width - 4, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(row, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 3, LV_PART_MAIN);
    for (unsigned i = 0; i < colorChoice::LockCount; ++i) {
      auto *swatch = lv_btn_create(row);
      _colors[i].set(swatch);
      lv_obj_set_size(swatch, 22, 22);
      styleColorSwatch(swatch, colorChoice::lock(i), state.color == colorChoice::lock(i));
      lv_obj_add_event_cb(swatch, event, LV_EVENT_CLICKED, this);
    }
  }
  if (caps.autoLock) {
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width - 4, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    auto *caption = label(row, TR("Lock when screen off"));
    lv_obj_set_flex_grow(caption, 1);
    auto *toggle = lv_switch_create(row);
    _locked.set(toggle);
    lv_obj_set_size(toggle, 44, 24);
    if (state.autoLock)
      lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle, event, LV_EVENT_VALUE_CHANGED, this);
  }
}
void LockSettingsScreen::event(lv_event_t *e) {
  auto &self = *static_cast<LockSettingsScreen *>(lv_event_get_user_data(e));
  if (!self._body.get())
    return;
  const auto generation = self._generation;
  auto *target = lv_event_get_target(e);
  if (target == self._locked.get() && lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    self._settings.setLocked(lv_obj_has_state(target, LV_STATE_CHECKED));
    return;
  }
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  if (target == self._wallpaper.get()) {
    if (self._host.openWallpaper)
      self._host.openWallpaper(self._host.context);
    return;
  }
  for (unsigned i = 0; i < colorChoice::LockCount; ++i) {
    if (target != self._colors[i].get())
      continue;
    const bool saved = self._settings.setColor(i);
    if (self._generation != generation || !self._body.get())
      return;
    const auto state = self._settings.read();
    for (unsigned j = 0; j < colorChoice::LockCount; ++j)
      if (self._colors[j].get())
        styleColorSwatch(self._colors[j].get(), colorChoice::lock(j), state.color == colorChoice::lock(j));
    if (self._host.alert)
      self._host.alert(self._host.context, saved ? TR("Lock text colour set") : TR("Save failed"), 1000);
    return;
  }
}
} // namespace screens
} // namespace ui
