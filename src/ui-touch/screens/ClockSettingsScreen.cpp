// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockSettingsScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void ClockSettingsScreen::detachCallbacks(lv_obj_t *root) {
  if (!root)
    return;
  while (lv_obj_remove_event_cb_with_user_data(root, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    detachCallbacks(lv_obj_get_child(root, i));
}
void ClockSettingsScreen::detach() {
  ++_generation;
  detachCallbacks(_body.get());
  _body.set(nullptr);
  _field.set(nullptr);
  _zoneCaption.set(nullptr);
  _offset.set(nullptr);
  for (auto &control : _controls)
    control.set(nullptr);
  closePicker();
}
void ClockSettingsScreen::closePicker() {
  ++_pickerGeneration;
  auto *old = _picker.get();
  detachCallbacks(old);
  _picker.set(nullptr);
  if (!old)
    return;
  if (_host.closeRoot)
    _host.closeRoot(&old);
  else
    lv_obj_del(old);
}
void ClockSettingsScreen::deleted(lv_event_t *event) {
  auto &self = *static_cast<ClockSettingsScreen *>(lv_event_get_user_data(event));
  // ObjectRef's earlier observer has already cleared the body. Detach must
  // not remove descriptors from this DELETE dispatch or LVGL 8 skips the
  // following observers (including the navigation root reference).
  self.detach();
}
void ClockSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
lv_obj_t *ClockSettingsScreen::label(const char *text) {
  auto *object = lv_label_create(_body.get());
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
lv_obj_t *ClockSettingsScreen::button(Control control, const char *text) {
  auto *object = lv_btn_create(_body.get());
  lv_obj_set_size(object, _width - 4, SC(34));
  styleButton(object);
  lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
  _controls[control].set(object);
  auto *caption = lv_label_create(object);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_obj_center(caption);
  return caption;
}
void ClockSettingsScreen::toggle(Control control, const char *text, bool on) {
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  auto *caption = lv_label_create(row);
  lv_obj_set_width(caption, _width - 64);
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_label_set_text(caption, text);
  auto *object = lv_switch_create(row);
  if (on)
    lv_obj_add_state(object, LV_STATE_CHECKED);
  lv_obj_add_event_cb(object, event, LV_EVENT_VALUE_CHANGED, this);
  _controls[control].set(object);
}
void ClockSettingsScreen::build(lv_obj_t *body, lv_coord_t width, bool bootSync) {
  const bool reuse = body && body == _body.get();
  const auto generation = _generation + 1;
  detach();
  if (_generation != generation || _body.get() || !body)
    return;
  if (reuse)
    lv_obj_clean(body);
  _width = width;
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(6), LV_PART_MAIN);
  const auto state = _settings.read();
  button(Sync, TR("Sync clock from system"));
  label(TR("Set the clock by hand (local time)"));
  _field.set(lv_textarea_create(body));
  lv_obj_set_size(_field.get(), _width - 4, SC(32));
  lv_textarea_set_one_line(_field.get(), true);
  lv_textarea_set_max_length(_field.get(), 16);
  taSetPlaceholder(_field.get(), "YYYY-MM-DD HH:MM");
  char text[20];
  _settings.prefill(text, sizeof text);
  if (_generation != generation || !_field.get())
    return;
  lv_textarea_set_text(_field.get(), text);
  if (_host.attachField)
    _host.attachField(_host.context, _field.get());
  if (_generation != generation || !_body.get())
    return;
  button(Manual, TR("Set clock"));
  if (bootSync) {
    toggle(Boot, TR("Sync time from saved Wi-Fi after cold boot"), state.bootWifi);
    toggle(Open, TR("Allow saved open networks for time sync"), state.openWifi);
    if (!state.bootWifi)
      lv_obj_add_state(_controls[Open].get(), LV_STATE_DISABLED);
  }
  toggle(Hour12, TR("12-hour clock"), state.hour12);
  label(TR("Time zone"));
  _zoneCaption.set(button(Zone, _settings.zoneLabel(state.zone)));
  lv_label_set_long_mode(_zoneCaption.get(), LV_LABEL_LONG_DOT);
  lv_obj_set_width(_zoneCaption.get(), lv_pct(92));
  lv_obj_set_style_text_align(_zoneCaption.get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  label(TR("Custom UTC offset (hours)"));
  auto *row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, SC(38));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  auto step = [&](Control which, const char *caption) {
    auto *object = lv_btn_create(row);
    lv_obj_set_size(object, SC(50), SC(34));
    styleButton(object);
    _controls[which].set(object);
    lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
    auto *text = lv_label_create(object);
    useChainedFont(text);
    lv_label_set_text(text, caption);
    lv_obj_center(text);
  };
  step(Minus, "-1 h");
  _offset.set(lv_label_create(row));
  lv_obj_set_style_text_color(_offset.get(), lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(_offset.get(), &font16(), LV_PART_MAIN);
  refreshOffset();
  step(Plus, "+1 h");
}
void ClockSettingsScreen::refreshOffset() {
  if (!_offset.get())
    return;
  const int offset = _settings.read().offset;
  char text[16];
  snprintf(text, sizeof text, offset ? "%+d h" : "%d h", offset);
  lv_label_set_text(_offset.get(), text);
}
void ClockSettingsScreen::event(lv_event_t *event) {
  auto &self = *static_cast<ClockSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  auto *target = lv_event_get_target(event);
  if (lv_obj_has_state(target, LV_STATE_DISABLED))
    return;
  int control = 0;
  while (control < Count && self._controls[control].get() != target)
    ++control;
  if (control == Count)
    return;
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
    if (control == Hour12) {
      self._settings.setFlag(ClockSettings::Flag::Hour12, on);
      self.notify(on ? TR("Clock: 12-hour") : TR("Clock: 24-hour"), 900);
    } else if (control == Boot) {
      self._settings.setFlag(ClockSettings::Flag::BootWifi, on);
      if (self._controls[Open].get()) {
        if (on)
          lv_obj_clear_state(self._controls[Open].get(), LV_STATE_DISABLED);
        else
          lv_obj_add_state(self._controls[Open].get(), LV_STATE_DISABLED);
      }
    } else if (control == Open)
      self._settings.setFlag(ClockSettings::Flag::OpenWifi, on);
    return;
  }
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  if (control == Sync) {
    const bool ok = self._settings.sync();
    self.notify(ok ? TR("Clock synced") : TR("No system time yet"), 900);
  } else if (control == Manual) {
    const auto generation = self._generation;
    if (self._host.syncKeyboard)
      self._host.syncKeyboard(self._host.context);
    if (generation != self._generation || !self._body.get() || !self._field.get())
      return;
    char text[20];
    snprintf(text, sizeof text, "%s", lv_textarea_get_text(self._field.get()));
    const auto result = self._settings.setManual(text);
    self.notify(result == clock::ParseResult::Ok       ? TR("Clock set")
                : result == clock::ParseResult::Format ? TR("Use YYYY-MM-DD HH:MM")
                                                       : TR("That is not a valid date and time"),
                result == clock::ParseResult::Ok ? 1400 : 1800);
  } else if (control == Zone)
    self.openPicker();
  else if (control == Minus || control == Plus) {
    const int offset = self._settings.stepOffset(control == Minus ? -1 : 1);
    self.refreshOffset();
    char text[48];
    snprintf(text, sizeof text, TR("Time offset: %+d h"), offset);
    self.notify(text, 900);
  }
}
void ClockSettingsScreen::picked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  auto &self = *static_cast<ClockSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get() || !self._picker.get())
    return;
  auto *target = lv_event_get_target(event);
  auto *parent = target;
  while (parent && parent != self._picker.get())
    parent = lv_obj_get_parent(parent);
  if (!parent)
    return;
  const int zone = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
  const auto generation = self._generation, pickerGeneration = self._pickerGeneration + 1;
  self.closePicker();
  if (self._generation != generation || self._pickerGeneration != pickerGeneration || self._picker.get() ||
      !self._body.get() || zone < 0 || zone >= self._settings.zoneCount())
    return;
  self._settings.setZone(zone);
  if (self._zoneCaption.get())
    lv_label_set_text(self._zoneCaption.get(), self._settings.zoneLabel(zone));
  char text[80];
  snprintf(text, sizeof text, TR("Time zone: %s"), self._settings.zoneLabel(zone));
  self.notify(text, 1100);
}
void ClockSettingsScreen::openPicker() {
  const auto generation = _generation, pickerGeneration = _pickerGeneration + 1;
  closePicker();
  if (_generation != generation || _pickerGeneration != pickerGeneration || !_body.get() || _picker.get())
    return;
  const lv_coord_t top = _host.contentTop ? _host.contentTop() : 0;
  if (_generation != generation || !_body.get())
    return;
  const lv_coord_t width = lv_disp_get_hor_res(nullptr), height = lv_disp_get_ver_res(nullptr) - top;
  _picker.set(lv_obj_create(lv_layer_top()));
  auto *root = _picker.get();
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, width, height);
  lv_obj_set_pos(root, 0, top);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto *title = lv_label_create(root);
  lv_label_set_text(title, TR("Time zone"));
  lv_obj_set_style_text_font(title, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 8);
  auto *close = lv_btn_create(root);
  lv_obj_set_size(close, SC(30), SC(26));
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(close);
  lv_obj_set_user_data(close, reinterpret_cast<void *>(intptr_t(-1)));
  lv_obj_add_event_cb(close, picked, LV_EVENT_CLICKED, this);
  auto *caption = lv_label_create(close);
  lv_label_set_text(caption, LV_SYMBOL_CLOSE);
  tanCloseRed(caption);
  lv_obj_set_style_text_font(caption, &font12(), LV_PART_MAIN);
  lv_obj_center(caption);
  auto *list = lv_obj_create(root);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, width - 12, height - 44);
  lv_obj_set_pos(list, 6, 40);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  const int current = _settings.read().zone;
  for (int i = 0; i < _settings.zoneCount(); ++i) {
    auto *row = lv_btn_create(list);
    lv_obj_set_size(row, width - 16, SC(40));
    styleButton(row);
    if (i == current)
      lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_color(
        row, lv_color_hex(i == current ? colors().COLOR_ON_ACCENT : colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(row, picked, LV_EVENT_CLICKED, this);
    auto *text = lv_label_create(row);
    lv_label_set_text(text, _settings.zoneLabel(i));
    lv_obj_set_style_text_font(text, &font14(), LV_PART_MAIN);
    lv_obj_center(text);
  }
  lv_obj_move_foreground(root);
}
} // namespace screens
} // namespace ui
