// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpsSettingsScreen.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void GpsSettingsScreen::detachCallbacks(lv_obj_t *root) {
  if (!root)
    return;
  while (lv_obj_remove_event_cb_with_user_data(root, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    detachCallbacks(lv_obj_get_child(root, i));
}
void GpsSettingsScreen::detach() {
  ++_generation;
  auto *dropdown = _baud.get();
  detachCallbacks(_body.get());
  _body.set(nullptr);
  _status.set(nullptr);
  _enabled.set(nullptr);
  _baud.set(nullptr);
  for (auto &button : _privacy)
    button.set(nullptr);
  if (dropdown)
    lv_dropdown_close(dropdown);
}
void GpsSettingsScreen::deleted(lv_event_t *event) {
  auto &self = *static_cast<GpsSettingsScreen *>(lv_event_get_user_data(event));
  // The preceding ObjectRef observer cleared the body, so detach cannot
  // shift the callback list while LVGL is still delivering DELETE.
  self.detach();
}
void GpsSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
lv_obj_t *GpsSettingsScreen::label(const char *text) {
  auto *object = lv_label_create(_body.get());
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
void GpsSettingsScreen::build(lv_obj_t *body, lv_coord_t width, bool hardware) {
  const bool reuse = body && body == _body.get();
  const auto generation = _generation + 1;
  detach();
  if (!body || _generation != generation || _body.get())
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  _width = width;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(6), LV_PART_MAIN);
  const auto state = _settings.read();
  if (_generation != generation || !_body.get())
    return;
  if (hardware) {
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width - 4, SC(34));
    auto *caption = lv_label_create(row);
    lv_label_set_text(caption, "GPS");
    lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(caption, LV_ALIGN_LEFT_MID, 0, 0);
    _enabled.set(lv_switch_create(row));
    lv_obj_align(_enabled.get(), LV_ALIGN_RIGHT_MID, 0, 0);
    if (state.position.enabled)
      lv_obj_add_state(_enabled.get(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(_enabled.get(), event, LV_EVENT_VALUE_CHANGED, this);
    _status.set(label(""));
    label(TR("GPS serial baud"));
    _baud.set(lv_dropdown_create(body));
    auto *dropdown = _baud.get();
    lv_obj_set_size(dropdown, width - 4, SC(34));
    lv_dropdown_set_options(dropdown, "9600\n19200\n38400\n57600\n115200");
    for (auto *object : {dropdown, lv_dropdown_get_list(dropdown)}) {
      lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(object, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_style_border_color(object, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    }
    for (unsigned i = 0; i < GpsSettings::BaudCount; ++i)
      if (state.baud == GpsSettings::baud(i))
        lv_dropdown_set_selected(dropdown, i);
    lv_obj_add_event_cb(dropdown, event, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(dropdown, dropdownOpened, LV_EVENT_CLICKED, this);
  } else
    label(TR("No GPS module on this device."));
  label(TR("Location privacy"));
  auto *row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, width - 4, SC(34));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, SC(4), LV_PART_MAIN);
  const char *labels[] = {TR("Exact"), "100 m", "250 m", "1 km"};
  for (unsigned i = 0; i < GpsSettings::PrivacyCount; ++i) {
    auto *button = lv_btn_create(row);
    _privacy[i].set(button);
    lv_obj_set_size(button, (width - 4 - SC(12)) / 4, SC(34));
    styleButton(button);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    auto *caption = lv_label_create(button);
    useChainedFont(caption);
    lv_label_set_text(caption, labels[i]);
    lv_obj_center(caption);
  }
  paintPrivacy(state.privacy);
  label(TR("position others see is shifted; your own map keeps the real fix"));
  refresh(platform::milliseconds());
}
void GpsSettingsScreen::paintPrivacy(uint16_t value) {
  for (unsigned i = 0; i < GpsSettings::PrivacyCount; ++i) {
    auto *button = _privacy[i].get();
    if (!button)
      continue;
    const bool selected = GpsSettings::privacy(i) == value;
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, selected ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        button, lv_color_hex(selected ? colors().COLOR_ON_ACCENT : colors().COLOR_TEXT), LV_PART_MAIN);
  }
}
void GpsSettingsScreen::refresh(uint32_t now) {
  if (!_body.get() || (_reading && _readingGeneration == _generation))
    return;
  const auto generation = _generation, previousGeneration = _readingGeneration;
  const bool wasReading = _reading;
  _reading = true;
  _readingGeneration = generation;
  const auto state = _settings.read();
  _reading = wasReading;
  _readingGeneration = previousGeneration;
  if (_generation != generation || !_body.get())
    return;
  if (_status.get()) {
    char text[240];
    _settings.format(state.position, now, false, text, sizeof text);
    if (strcmp(lv_label_get_text(_status.get()), text))
      lv_label_set_text(_status.get(), text);
  }
  if (_enabled.get()) {
    if (state.position.enabled)
      lv_obj_add_state(_enabled.get(), LV_STATE_CHECKED);
    else
      lv_obj_clear_state(_enabled.get(), LV_STATE_CHECKED);
  }
  paintPrivacy(state.privacy);
}
void GpsSettingsScreen::dropdownOpened(lv_event_t *event) {
  auto &self = *static_cast<GpsSettingsScreen *>(lv_event_get_user_data(event));
  if (self._body.get() && lv_event_get_target(event) == self._baud.get() && self._host.clampDropdown)
    self._host.clampDropdown(event);
}
void GpsSettingsScreen::event(lv_event_t *event) {
  auto &self = *static_cast<GpsSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  auto *target = lv_event_get_target(event);
  const auto generation = self._generation;
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    if (target == self._enabled.get()) {
      const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
      self._settings.setEnabled(on);
      if (self._generation != generation || !self._body.get())
        return;
      self.refresh(platform::milliseconds());
    } else if (target == self._baud.get()) {
      const auto index = lv_dropdown_get_selected(target);
      if (!self._settings.setBaud(index))
        return;
      char text[64];
      snprintf(text, sizeof text, "GPS %lu baud — reboot to apply",
               static_cast<unsigned long>(GpsSettings::baud(index)));
      self.notify(text, 1800);
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    for (unsigned i = 0; i < GpsSettings::PrivacyCount; ++i) {
      if (target != self._privacy[i].get())
        continue;
      if (!self._settings.setPrivacy(i))
        return;
      self.paintPrivacy(GpsSettings::privacy(i));
      self.notify(i ? TR("Advertised position displaced") : TR("Advertising your exact position"), 1400);
      return;
    }
  }
}
} // namespace screens
} // namespace ui
