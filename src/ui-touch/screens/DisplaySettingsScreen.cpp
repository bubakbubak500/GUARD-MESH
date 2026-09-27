// SPDX-License-Identifier: GPL-3.0-or-later
#include "DisplaySettingsScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void DisplaySettingsScreen::unbind(lv_obj_t *root) {
  if (!root)
    return;
  while (lv_obj_remove_event_cb_with_user_data(root, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    unbind(lv_obj_get_child(root, i));
}
void DisplaySettingsScreen::detach() {
  ++_generation;
  unbind(_body.get());
  _body.set(nullptr);
  unbind(_timeout.get());
  _timeout.set(nullptr);
  auto *dropdown = _size.get();
  unbind(dropdown);
  _size.set(nullptr);
  for (auto &object : _flags) {
    unbind(object.get());
    object.set(nullptr);
  }
  for (auto &object : _theme) {
    unbind(object.get());
    object.set(nullptr);
  }
  unbind(_accent.get());
  _accent.set(nullptr);
  unbind(_rotation.get());
  _rotation.set(nullptr);
  if (dropdown)
    lv_dropdown_close(dropdown);
}
void DisplaySettingsScreen::deleted(lv_event_t *e) {
  static_cast<DisplaySettingsScreen *>(lv_event_get_user_data(e))->detach();
}
void DisplaySettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
lv_obj_t *DisplaySettingsScreen::label(const char *text) {
  auto *object = lv_label_create(_body.get());
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
lv_obj_t *DisplaySettingsScreen::button(lv_obj_t *parent, const char *text, lv_coord_t width) {
  auto *object = lv_btn_create(parent);
  lv_obj_set_size(object, width, SC(34));
  styleButton(object);
  auto *caption = lv_label_create(object);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_obj_center(caption);
  lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
  return object;
}
void DisplaySettingsScreen::flag(DisplaySettings::Flag flag, const char *text,
                                 const DisplaySettings::State &state) {
  if (!_settings.supports(flag))
    return;
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  auto *caption = lv_label_create(row);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_flex_grow(caption, 1);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  auto *object = lv_switch_create(row);
  lv_obj_set_size(object, 44, 24);
  _flags[flag].set(object);
  if (state.flags[flag])
    lv_obj_add_state(object, LV_STATE_CHECKED);
  if (flag == DisplaySettings::GlanceLocked && !state.flags[DisplaySettings::Glance])
    lv_obj_add_state(object, LV_STATE_DISABLED);
  lv_obj_add_event_cb(object, event, LV_EVENT_VALUE_CHANGED, this);
}
void DisplaySettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  const bool reuse = body && body == _body.get();
  detach();
  if (!body)
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  _width = width;
  const auto generation = _generation;
  const auto state = _settings.read();
  if (_generation != generation || !_body.get())
    return;
  const auto caps = _settings.capabilities();
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(10), LV_PART_MAIN);
  label(TR("Screen timeout (s, 0 = never, min 10)"));
  auto *field = lv_textarea_create(body);
  _timeout.set(field);
  lv_obj_set_size(field, SC(100), SC(30));
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, 4);
  lv_textarea_set_accepted_chars(field, "0123456789");
  char text[40];
  snprintf(text, sizeof text, "%u", unsigned(state.timeout));
  lv_textarea_set_text(field, text);
  if (_host.attachField)
    _host.attachField(_host.context, field);
  if (_generation != generation || !_body.get() || _timeout.get() != field)
    return;
  lv_obj_add_event_cb(field, event, LV_EVENT_DEFOCUSED, this);
  if (caps.size) {
    label(TR("UI size (restart to apply)"));
    auto *size = lv_dropdown_create(body);
    _size.set(size);
    lv_dropdown_set_options(size, caps.pagerSizes ? TR("Small\nMedium\nLarge\nJumbo")
                                                  : TR("Normal (100%)\nLarge (150%)\nHuge (200%)"));
    lv_dropdown_set_selected(size, state.size);
    lv_obj_set_width(size, width - 4);
    lv_obj_add_event_cb(size, event, LV_EVENT_ALL, this);
  }
  flag(DisplaySettings::MessageLed, TR("Message LED"), state);
  flag(DisplaySettings::Miles, TR("Distance in miles"), state);
  flag(DisplaySettings::Colorful, TR("Colourful chat bubbles"), state);
  flag(DisplaySettings::Compact, TR("Compact messages (IRC style)"), state);
  flag(DisplaySettings::HideName, TR("Hide device name"), state);
  flag(DisplaySettings::Glance, TR("At a glance"), state);
  flag(DisplaySettings::GlanceLocked, TR("At a glance while locked"), state);
  flag(DisplaySettings::Sensors, TR("Show Sensors tab"), state);
  if (caps.sensors)
    label(TR("Applies after restart."));
  if (caps.appearance) {
    label(TR("Appearance"));
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width - 4, SC(34));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, SC(4), LV_PART_MAIN);
    for (unsigned i = 0; i < 2; ++i) {
      snprintf(text, sizeof text, "%s%s", state.theme == i ? LV_SYMBOL_OK "  " : "",
               i ? TR("Day") : TR("Night"));
      _theme[i].set(button(row, text, (width - 4 - SC(4)) / 2));
    }
    label(TR("Accent colour"));
    auto *accentRow = lv_obj_create(body);
    lv_obj_remove_style_all(accentRow);
    lv_obj_set_size(accentRow, width - 4, SC(34));
    lv_obj_set_flex_flow(accentRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(accentRow, SC(6), LV_PART_MAIN);
    _accent.set(button(accentRow, TR("Pick colour"), width - 4 - SC(36)));
    auto *swatch = lv_obj_create(accentRow);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, SC(30), SC(30));
    lv_obj_set_style_radius(swatch, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
  }
  if (caps.rotation) {
    label(TR("Orientation (tap to rotate, reboots)"));
    _rotation.set(button(body, state.rotation ? "Screen: Landscape" : "Screen: Portrait", width - 4));
  }
}
void DisplaySettingsScreen::flagNotice(DisplaySettings::Flag flag, bool on) {
  switch (flag) {
  case DisplaySettings::Miles:
    notify(on ? TR("Distance: miles") : TR("Distance: km"), 900);
    break;
  case DisplaySettings::Colorful:
    notify(on ? TR("Taste the rainbow!") : TR("Chat bubbles: plain"), on ? 1500 : 900);
    break;
  case DisplaySettings::Compact:
    notify(on ? TR("Compact messages") : TR("Chat bubbles"), 1000);
    break;
  case DisplaySettings::HideName:
    notify(on ? TR("Device name hidden") : TR("Device name shown"), 1000);
    break;
  case DisplaySettings::Glance:
    notify(on ? TR("At a glance enabled") : TR("At a glance disabled"));
    break;
  case DisplaySettings::GlanceLocked:
    notify(on ? TR("Glance shows message previews\nwhile the screen is locked")
              : TR("Glance only shows when unlocked"),
           1600);
    break;
  case DisplaySettings::MessageLed:
    notify(on ? TR("Message LED on") : TR("Message LED off"), 900);
    break;
  case DisplaySettings::Sensors:
    notify(on ? TR("Sensors tab: on (restart to apply)") : TR("Sensors tab: off (restart to apply)"), 1800);
    break;
  default:
    break;
  }
}
void DisplaySettingsScreen::event(lv_event_t *e) {
  auto &self = *static_cast<DisplaySettingsScreen *>(lv_event_get_user_data(e));
  if (!self._body.get())
    return;
  const auto generation = self._generation;
  auto *target = lv_event_get_target(e);
  const auto code = lv_event_get_code(e);
  if (target == self._timeout.get() && code == LV_EVENT_DEFOCUSED) {
    if (self._host.deleting && self._host.deleting(e))
      return;
    if (self._host.syncField)
      self._host.syncField(self._host.context);
    if (self._generation != generation || self._timeout.get() != target)
      return;
    uint16_t seconds = 0;
    const auto result = self._settings.setTimeout(lv_textarea_get_text(target), seconds);
    if (self._generation != generation || self._timeout.get() != target)
      return;
    if (result != DisplaySettings::Result::Applied) {
      self.notify(TR("Save failed"));
      return;
    }
    char text[48];
    snprintf(text, sizeof text, "%u", unsigned(seconds));
    lv_textarea_set_text(target, text);
    if (self._generation != generation || self._timeout.get() != target)
      return;
    if (seconds)
      snprintf(text, sizeof text, TR("Screen timeout: %ds"), int(seconds));
    else
      snprintf(text, sizeof text, "%s", TR("Screen timeout: never"));
    self.notify(text);
    return;
  }
  if (target == self._size.get()) {
    if (code == LV_EVENT_CLICKED && self._host.clampDropdown) {
      self._host.clampDropdown(e);
      return;
    }
    if (code != LV_EVENT_VALUE_CHANGED)
      return;
    const auto result = self._settings.setSize(lv_dropdown_get_selected(target));
    if (self._generation == generation && self._body.get())
      self.notify(result == DisplaySettings::Result::Applied ? TR("UI size saved — restart to apply")
                                                             : TR("Save failed"),
                  2200);
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    for (unsigned i = 0; i < DisplaySettings::FlagCount; ++i) {
      if (target != self._flags[i].get())
        continue;
      const auto flag = static_cast<DisplaySettings::Flag>(i);
      // A disabled privacy control must not bypass its master via a queued event.
      if (lv_obj_has_state(target, LV_STATE_DISABLED))
        return;
      const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
      const bool saved = self._settings.setFlag(flag, on);
      if (self._generation != generation || !self._body.get())
        return;
      if (flag == DisplaySettings::Glance && self._flags[DisplaySettings::GlanceLocked].get()) {
        auto *locked = self._flags[DisplaySettings::GlanceLocked].get();
        if (on)
          lv_obj_clear_state(locked, LV_STATE_DISABLED);
        else
          lv_obj_add_state(locked, LV_STATE_DISABLED);
      }
      if (saved)
        self.flagNotice(flag, on);
      else
        self.notify(TR("Save failed"));
      return;
    }
  }
  if (code != LV_EVENT_CLICKED)
    return;
  if (target == self._accent.get()) {
    if (self._host.accentPicker)
      self._host.accentPicker(self._host.context);
    return;
  }
  DisplaySettings::Result result = DisplaySettings::Result::Unchanged;
  if (target == self._rotation.get())
    result = self._settings.rotate();
  for (unsigned i = 0; i < 2; ++i)
    if (target == self._theme[i].get()) {
      result = self._settings.setTheme(i);
      break;
    }
  if (self._generation == generation && self._body.get() && result == DisplaySettings::Result::Failed)
    self.notify(TR("Save failed"));
}
} // namespace screens
} // namespace ui
