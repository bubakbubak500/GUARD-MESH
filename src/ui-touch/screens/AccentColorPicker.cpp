// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccentColorPicker.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ColorSwatch.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void AccentColorPicker::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}
void AccentColorPicker::detach() {
  ++_generation;
  _syncing = false;
  unbind(_root.get());
  _root.set(nullptr);
  unbind(_field.get());
  _field.set(nullptr);
  _preview.set(nullptr);
  for (auto &object : _swatches) {
    unbind(object.get());
    object.set(nullptr);
  }
  unbind(_save.get());
  _save.set(nullptr);
  unbind(_reset.get());
  _reset.set(nullptr);
  unbind(_close.get());
  _close.set(nullptr);
}
void AccentColorPicker::close() {
  auto *old = _root.get();
  detach();
  if (old) {
    if (_host.closeRoot)
      _host.closeRoot(&old);
    else
      lv_obj_del(old);
  }
}
void AccentColorPicker::deleted(lv_event_t *e) {
  static_cast<AccentColorPicker *>(lv_event_get_user_data(e))->detach();
}
void AccentColorPicker::select(uint32_t value, bool updateField) {
  _value = value & 0xFFFFFF;
  if (_preview.get())
    lv_obj_set_style_bg_color(_preview.get(), lv_color_hex(accentClampReadable(_value)), LV_PART_MAIN);
  if (!updateField || !_field.get())
    return;
  const auto generation = _generation;
  _syncing = true;
  char text[8];
  snprintf(text, sizeof text, "%06X", unsigned(_value));
  lv_textarea_set_text(_field.get(), text);
  if (generation == _generation)
    _syncing = false;
}
void AccentColorPicker::save() {
  const auto generation = _generation;
  if (_host.syncField)
    _host.syncField(_host.context);
  if (generation != _generation || !_root.get() || !_field.get())
    return;
  uint32_t value = 0;
  if (!colorChoice::parseHex(lv_textarea_get_text(_field.get()), value)) {
    if (_host.alert)
      _host.alert(_host.context, TR("Save failed"), 1600);
    return;
  }
  const bool saved = _host.save && _host.save(_host.context, value);
  if (generation != _generation || !_root.get())
    return;
  if (!saved) {
    if (_host.alert)
      _host.alert(_host.context, TR("Save failed"), 1600);
    return;
  }
  close();
  // A close hook can open another picker. Do not apply/reboot on behalf of an
  // old generation or close a replacement created during persistence.
  if (_generation != generation + 1)
    return;
  if (_host.apply)
    _host.apply(_host.context, value);
  if (_generation != generation + 1)
    return;
  if (_host.restart)
    _host.restart(_host.context);
}
void AccentColorPicker::event(lv_event_t *e) {
  auto &self = *static_cast<AccentColorPicker *>(lv_event_get_user_data(e));
  if (!self._root.get())
    return;
  auto *target = lv_event_get_target(e);
  if (target == self._field.get() && lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    uint32_t value;
    if (!self._syncing && colorChoice::parseHex(lv_textarea_get_text(target), value))
      self.select(value, false);
    return;
  }
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  if (target == self._close.get()) {
    self.close();
    return;
  }
  if (target == self._save.get()) {
    self.save();
    return;
  }
  if (target == self._reset.get()) {
    self.select(colorChoice::DefaultAccent, true);
    return;
  }
  for (unsigned i = 0; i < colorChoice::AccentCount; ++i)
    if (target == self._swatches[i].get()) {
      self.select(colorChoice::accent(i), true);
      return;
    }
}
void AccentColorPicker::open() {
  if (_destroying)
    return;
  const auto expected = _generation + 1;
  close();
  if (_generation != expected)
    return;
  const auto value = _host.read ? _host.read(_host.context) : colorChoice::DefaultAccent;
  if (_generation != expected)
    return;
  const auto top = _host.contentTop ? _host.contentTop() : 0;
  if (_generation != expected)
    return;
  const lv_coord_t width = lv_disp_get_hor_res(nullptr), height = lv_disp_get_ver_res(nullptr) - top;
  auto *root = lv_obj_create(lv_layer_top());
  _root.set(root);
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, width, height);
  lv_obj_set_pos(root, 0, top);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(root, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_top(root, 3, LV_PART_MAIN);
  lv_obj_set_scroll_dir(root, LV_DIR_VER);
  auto *title = lv_label_create(root);
  lv_label_set_text(title, TR("Accent colour"));
  lv_obj_set_style_text_font(title, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  auto *close = lv_btn_create(root);
  _close.set(close);
  lv_obj_add_flag(close, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(close, 30, 26);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 2);
  styleButton(close);
  lv_obj_add_event_cb(close, event, LV_EVENT_CLICKED, this);
  auto *caption = lv_label_create(close);
  lv_label_set_text(caption, LV_SYMBOL_CLOSE);
  tanCloseRed(caption);
  lv_obj_set_style_text_font(caption, &font12(), LV_PART_MAIN);
  lv_obj_center(caption);
  auto *grid = lv_obj_create(root);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, width - 24, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_column(grid, 8, LV_PART_MAIN);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  for (unsigned i = 0; i < colorChoice::AccentCount; ++i) {
    auto *swatch = lv_btn_create(grid);
    _swatches[i].set(swatch);
    lv_obj_set_size(swatch, SC(28), SC(28));
    styleColorSwatch(swatch, colorChoice::accent(i));
    lv_obj_add_event_cb(swatch, event, LV_EVENT_CLICKED, this);
  }
  auto *row = lv_obj_create(root);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, SC(150), SC(30));
  auto *hash = lv_label_create(row);
  useChainedFont(hash);
  lv_label_set_text(hash, "#");
  lv_obj_set_style_text_color(hash, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(hash, LV_ALIGN_LEFT_MID, 2, 0);
  auto *field = lv_textarea_create(row);
  _field.set(field);
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, 6);
  lv_textarea_set_accepted_chars(field, "0123456789abcdefABCDEF");
  lv_obj_set_size(field, SC(120), SC(28));
  lv_obj_align(field, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_add_event_cb(field, event, LV_EVENT_VALUE_CHANGED, this);
  auto *preview = lv_obj_create(root);
  _preview.set(preview);
  lv_obj_remove_style_all(preview);
  lv_obj_set_size(preview, SC(150), SC(28));
  lv_obj_set_style_radius(preview, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, LV_PART_MAIN);
  caption = lv_label_create(preview);
  useChainedFont(caption);
  lv_label_set_text(caption, TR("Sample text"));
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(caption);
  auto *actions = lv_obj_create(root);
  lv_obj_remove_style_all(actions);
  lv_obj_set_size(actions, width - 24, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(actions, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(actions, 6, LV_PART_MAIN);
  lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  auto button = [&](const char *text) {
    auto *b = lv_btn_create(actions);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_max_width(b, LV_PCT(100), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 12, LV_PART_MAIN);
    styleButton(b);
    lv_obj_add_event_cb(b, event, LV_EVENT_CLICKED, this);
    auto *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
    useChainedFont(l);
    lv_obj_center(l);
    return b;
  };
  _save.set(button(TR("Save & restart")));
  _reset.set(button(TR("Reset")));
  select(value, true);
  if (_generation == expected && _field.get() == field && _host.attachField)
    _host.attachField(_host.context, field);
}
} // namespace screens
} // namespace ui
