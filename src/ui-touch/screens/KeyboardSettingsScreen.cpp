// SPDX-License-Identifier: GPL-3.0-or-later
#include "KeyboardSettingsScreen.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void KeyboardSettingsScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}
void KeyboardSettingsScreen::detach() {
  ++_generation;
  _capture = -1;
  unbind(_body.get());
  _body.set(nullptr);
  // Root ObjectRef precedes deleted(), so this only retires descendants during
  // DELETE and leaves the currently dispatched root callback array intact.
  unbind(_light.get());
  _light.set(nullptr);
  unbind(_slider.get());
  _slider.set(nullptr);
  unbind(_mode.get());
  _mode.set(nullptr);
  _modeLabel.set(nullptr);
  for (auto &object : _presets) {
    unbind(object.get());
    object.set(nullptr);
  }
  for (auto &object : _flags) {
    unbind(object.get());
    object.set(nullptr);
  }
  for (auto &object : _layouts) {
    unbind(object.get());
    object.set(nullptr);
  }
  for (auto &object : _bindings) {
    unbind(object.get());
    object.set(nullptr);
  }
  for (auto &object : _keyLabels)
    object.set(nullptr);
}
void KeyboardSettingsScreen::deleted(lv_event_t *event) {
  static_cast<KeyboardSettingsScreen *>(lv_event_get_user_data(event))->detach();
}
void KeyboardSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
lv_obj_t *KeyboardSettingsScreen::label(lv_obj_t *parent, const char *text) {
  auto *object = lv_label_create(parent);
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
lv_obj_t *KeyboardSettingsScreen::toggle(const char *text) {
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  auto *caption = label(row, text);
  lv_obj_set_flex_grow(caption, 1);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  auto *object = lv_switch_create(row);
  lv_obj_set_size(object, 44, 24);
  return object;
}
void KeyboardSettingsScreen::flag(KeyboardSettings::Flag id, const char *text, const char *hint) {
  if (!_settings.supports(id))
    return;
  _flags[id].set(toggle(text));
  lv_obj_add_event_cb(_flags[id].get(), event, LV_EVENT_VALUE_CHANGED, this);
  if (hint)
    label(_body.get(), hint);
}
void KeyboardSettingsScreen::binding(unsigned index) {
  if (!_settings.supportsBinding(index))
    return;
  auto *row = lv_btn_create(_body.get());
  _bindings[index].set(row);
  lv_obj_set_size(row, _width - 4, SC(34));
  styleButton(row);
  lv_obj_add_event_cb(row, event, LV_EVENT_CLICKED, this);
  auto *name = label(row, TR(KeyBindings::name(index)));
  lv_obj_set_width(name, _width - SC(58));
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
  auto *value = lv_label_create(row);
  _keyLabels[index].set(value);
  lv_obj_set_style_text_font(value, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
}
void KeyboardSettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  const bool reuse = body && body == _body.get();
  detach();
  if (!body)
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  _width = width;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(8), LV_PART_MAIN);
  const auto caps = _settings.capabilities();
  if (caps.light != KeyboardSettings::Light::None) {
    auto *group = lv_obj_create(body);
    _light.set(group);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, width - 4, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(group, SC(12), LV_PART_MAIN);
    label(group, TR("Keyboard backlight"));
    if (caps.light == KeyboardSettings::Light::Brightness) {
      auto *slider = lv_slider_create(group);
      _slider.set(slider);
      lv_obj_set_size(slider, width - SC(18), SC(8));
      lv_slider_set_range(slider, 1, 100);
      lv_obj_set_style_bg_color(slider, lv_color_hex(colors().COLOR_TRACK), LV_PART_MAIN);
      lv_obj_set_style_bg_color(slider, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(slider, lv_color_hex(colors().COLOR_ACCENT), LV_PART_KNOB);
      lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
      lv_obj_add_event_cb(slider, event, LV_EVENT_VALUE_CHANGED, this);
      auto *presets = lv_obj_create(group);
      lv_obj_remove_style_all(presets);
      lv_obj_set_size(presets, width - 4, SC(34));
      lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(presets, SC(4), LV_PART_MAIN);
      const char *names[] = {"Off", "25", "50", "75", "Max"};
      for (unsigned i = 0; i < 5; ++i) {
        auto *button = lv_btn_create(presets);
        _presets[i].set(button);
        lv_obj_set_size(button, (width - 4 - SC(16)) / 5, SC(30));
        styleButton(button);
        auto *caption = lv_label_create(button);
        useChainedFont(caption);
        lv_label_set_text(caption, names[i]);
        lv_obj_center(caption);
        lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
      }
    } else {
      auto *button = lv_btn_create(group);
      _mode.set(button);
      lv_obj_set_size(button, width - 4, SC(34));
      styleButton(button);
      _modeLabel.set(lv_label_create(button));
      useChainedFont(_modeLabel.get());
      lv_obj_center(_modeLabel.get());
      lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
      label(group, "tap to cycle off / on / auto");
    }
  }
  label(body, TR("Secondary keyboards"));
  label(body, caps.physicalCycle
                  ? "double-tap SPACE cycles through the ones you enable"
                  : "tap the language key (e.g. EN) on the keyboard to cycle the ones you enable");
  for (unsigned i = 1; i < KEYBOARD_LAYOUT_COUNT; ++i) {
    _layouts[i].set(toggle(KeyboardSettings::layoutName(i)));
    lv_obj_add_event_cb(_layouts[i].get(), event, LV_EVENT_VALUE_CHANGED, this);
  }
  flag(KeyboardSettings::Accent, TR("Accent popups"),
       TR("pick accented letters as you type; off = plain typing"));
  flag(KeyboardSettings::EnterSends, TR("Enter key sends message"),
       TR("off = Enter adds a new line; tap Send to send"));
  flag(KeyboardSettings::Flash, TR("Flash on new message"),
       TR("lights the keyboard + wakes the screen on an incoming message"));
  flag(KeyboardSettings::Legacy, TR("Older keyboard protocol"),
       TR("turn on if your keyboard types the wrong letters; disables modifier latching"));
  flag(KeyboardSettings::KeyboardNav, TR("Keyboard navigation"),
       TR("drive the UI without the screen; keys still type in text fields"));
  flag(KeyboardSettings::TrackballNav, TR("Trackball navigates UI"),
       TR("Experimental - has known issues. Off = soft mouse cursor."));
  flag(KeyboardSettings::MenuLetters, TR("Show menu-bar letters"));
  if (caps.navigation == KeyboardSettings::Navigation::All) {
    label(body, TR("Tab hotkeys — tap a row, then press a key"));
    for (unsigned i = 0; i < KeyBindings::Tabs; ++i)
      binding(i);
    label(body, TR("Navigation keys — tap a row, then press a key"));
    for (unsigned i = KeyBindings::Tabs; i < KeyBindings::Count; ++i)
      binding(i);
  } else if (caps.navigation == KeyboardSettings::Navigation::ExtraDirections) {
    label(body, TR("Navigation keys"));
    label(body, TR("Arrows + Enter always work. These letters are extra — tap a row, then press a key."));
    const unsigned order[] = {5, 7, 9, 6, 8, 11, 12};
    for (auto index : order)
      binding(index);
  }
  flag(KeyboardSettings::Reverse, TR("Reverse scrollball"));
  flag(KeyboardSettings::Edge, TR("Edge scroll"), TR("push past a screen edge to scroll content"));
  refresh();
}
void KeyboardSettingsScreen::refresh() {
  if (!_body.get())
    return;
  const auto state = _settings.read();
  for (unsigned i = 0; i < KeyboardSettings::FlagCount; ++i) {
    if (!_flags[i].get())
      continue;
    if (state.flags[i])
      lv_obj_add_state(_flags[i].get(), LV_STATE_CHECKED);
    else
      lv_obj_clear_state(_flags[i].get(), LV_STATE_CHECKED);
  }
  for (unsigned i = 1; i < KEYBOARD_LAYOUT_COUNT; ++i) {
    if (!_layouts[i].get())
      continue;
    if (state.layouts & (1u << i))
      lv_obj_add_state(_layouts[i].get(), LV_STATE_CHECKED);
    else
      lv_obj_clear_state(_layouts[i].get(), LV_STATE_CHECKED);
  }
  if (_light.get()) {
    if (_settings.capabilities().legacy && state.flags[KeyboardSettings::Legacy])
      lv_obj_add_flag(_light.get(), LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_clear_flag(_light.get(), LV_OBJ_FLAG_HIDDEN);
  }
  if (_slider.get())
    lv_slider_set_value(_slider.get(), state.brightness, LV_ANIM_OFF);
  if (_modeLabel.get())
    lv_label_set_text(_modeLabel.get(), state.lightMode == 0 ? "Off" : state.lightMode == 1 ? "On" : "Auto");
  for (unsigned i = 0; i < KeyBindings::Count; ++i)
    if (_keyLabels[i].get()) {
      const int key = KeyBindings::lower(state.keys[i]);
      char text[] = {char(key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key ? key : '-'), 0};
      lv_label_set_text(_keyLabels[i].get(), text);
    }
}
bool KeyboardSettingsScreen::captureKey(int key) {
  if (!capturing())
    return false;
  const auto generation = _generation;
  const unsigned index = _capture;
  _capture = -1;
  const auto result = _settings.assign(index, key);
  if (_generation != generation || !_body.get())
    return true;
  refresh();
  switch (result) {
  case KeyBindings::Result::Ok:
    notify(TR("Hotkey saved"), 800);
    break;
  case KeyBindings::Result::Cancelled:
    notify(TR("Cancelled"), 700);
    break;
  case KeyBindings::Result::Invalid:
    notify(TR("Letters only"), 900);
    break;
  case KeyBindings::Result::Duplicate:
    notify(TR("Key already in use"), 1100);
    break;
  case KeyBindings::Result::SaveFailed:
    notify(TR("Save failed"), 1800);
    break;
  default:
    break;
  }
  return true;
}
void KeyboardSettingsScreen::flagNotice(KeyboardSettings::Flag flag, bool on) {
  switch (flag) {
  case KeyboardSettings::Accent:
    notify(on ? TR("Accent popups: on") : TR("Accent popups: off"), 1100);
    break;
  case KeyboardSettings::EnterSends:
    notify(on ? TR("Enter sends messages") : TR("Enter adds a new line"), 1200);
    break;
  case KeyboardSettings::Legacy:
    notify(on ? TR("Using the older keyboard protocol") : TR("Detecting the keyboard protocol"), 1600);
    break;
  case KeyboardSettings::KeyboardNav:
    notify(on ? TR("Keyboard nav: WASDZ on") : TR("Keyboard nav: off"), 1100);
    break;
  case KeyboardSettings::TrackballNav:
    notify(on ? TR("Trackball: navigate UI") : TR("Trackball: cursor"), 1100);
    break;
  case KeyboardSettings::Reverse:
    notify(on ? TR("Scrollball: reversed") : TR("Scrollball: normal"), 900);
    break;
  default:
    break;
  }
}
void KeyboardSettingsScreen::event(lv_event_t *event) {
  auto &self = *static_cast<KeyboardSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  const auto generation = self._generation, now = platform::milliseconds();
  auto *target = lv_event_get_target(event);
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (target == self._slider.get()) {
      self._settings.setBrightness(lv_slider_get_value(target), now);
      return;
    }
    const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
    for (unsigned i = 0; i < KeyboardSettings::FlagCount; ++i) {
      if (target != self._flags[i].get())
        continue;
      const auto flag = static_cast<KeyboardSettings::Flag>(i);
      const bool saved = self._settings.setFlag(flag, on);
      if (self._generation != generation || !self._body.get())
        return;
      self.refresh();
      if (self._host.flagApplied)
        self._host.flagApplied(self._host.context, flag, on, target);
      if (self._generation != generation || !self._body.get())
        return;
      if (saved)
        self.flagNotice(flag, on);
      else
        self.notify(TR("Save failed"), 1800);
      return;
    }
    for (unsigned i = 1; i < KEYBOARD_LAYOUT_COUNT; ++i)
      if (target == self._layouts[i].get()) {
        const bool saved = self._settings.setLayout(i, on);
        if (self._generation != generation || !self._body.get())
          return;
        self.refresh();
        char text[48];
        snprintf(text, sizeof text, "%s: %s", keyboardLayoutName(static_cast<KeyboardLayoutId>(i)),
                 on ? "on" : "off");
        self.notify(saved ? text : TR("Save failed"), 800);
        return;
      }
  }
  if (code != LV_EVENT_CLICKED)
    return;
  for (unsigned i = 0; i < KeyBindings::Count; ++i)
    if (target == self._bindings[i].get()) {
      self._capture = i;
      self.notify(TR("Press a key…"), 4000);
      return;
    }
  if (target == self._mode.get()) {
    self._settings.cycleLight(now);
    self._settings.saveLight();
  } else {
    for (unsigned i = 0; i < 5; ++i)
      if (target == self._presets[i].get()) {
        if (i)
          self._settings.setBrightness(i * 25, now);
        else
          self._settings.lightOff(now);
        break;
      }
  }
  if (self._generation == generation && self._body.get())
    self.refresh();
}
} // namespace screens
} // namespace ui
