// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundSettingsScreen.h"
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
void SoundSettingsScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}
void SoundSettingsScreen::clearMenuRefs() {
  unbind(_menu.get());
  unbind(_menuFiles.get());
  unbind(_menuBuiltin.get());
  unbind(_menuClose.get());
  _menu.set(nullptr);
  _menuFiles.set(nullptr);
  _menuBuiltin.set(nullptr);
  _menuClose.set(nullptr);
}
void SoundSettingsScreen::closeMenu() {
  ++_menuGeneration;
  auto *root = _menu.get();
  clearMenuRefs();
  if (root) {
    if (_host.closeRoot)
      _host.closeRoot(&root);
    else
      lv_obj_del(root);
  }
}
void SoundSettingsScreen::detach() {
  ++_generation;
  unbind(_body.get());
  _body.set(nullptr);
  // The body ObjectRef has already cleared during DELETE, so retire each
  // child explicitly without shifting the body's active event descriptors.
  for (auto &flag : _flags) {
    unbind(flag.get());
    flag.set(nullptr);
  }
  for (auto &row : _stepButtons)
    for (auto &button : row) {
      unbind(button.get());
      button.set(nullptr);
    }
  for (auto &value : _values)
    value.set(nullptr);
  for (auto &button : _files) {
    unbind(button.get());
    button.set(nullptr);
  }
  for (auto &label : _fileLabels)
    label.set(nullptr);
  for (auto &row : _swatches)
    for (auto &button : row) {
      unbind(button.get());
      button.set(nullptr);
    }
  closeMenu();
}
void SoundSettingsScreen::deleted(lv_event_t *event) {
  static_cast<SoundSettingsScreen *>(lv_event_get_user_data(event))->detach();
}
void SoundSettingsScreen::menuDeleted(lv_event_t *event) {
  auto &self = *static_cast<SoundSettingsScreen *>(lv_event_get_user_data(event));
  ++self._menuGeneration;
  self.clearMenuRefs();
}
void SoundSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
void SoundSettingsScreen::preview(unsigned slot, bool announce) {
  const auto generation = _generation;
  const auto result = _settings.preview(slot);
  if (!announce || _generation != generation || !_body.get())
    return;
  if (result == SoundSettings::Preview::Quiet)
    notify(TR("Sound is off"), 1400);
  else if (result == SoundSettings::Preview::DoNotDisturb)
    notify(TR("Do Not Disturb is on"), 1400);
}
lv_obj_t *SoundSettingsScreen::label(lv_obj_t *parent, const char *text) {
  auto *object = lv_label_create(parent);
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
lv_obj_t *SoundSettingsScreen::button(lv_obj_t *parent, const char *text, lv_coord_t width) {
  auto *object = lv_btn_create(parent);
  lv_obj_set_size(object, width, SC(34));
  styleButton(object);
  auto *caption = lv_label_create(object);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_obj_set_width(caption, lv_pct(92));
  lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(caption);
  return object;
}
void SoundSettingsScreen::toggle(SoundSettings::Flag flag, const char *text) {
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  auto *caption = label(row, text);
  lv_obj_set_flex_grow(caption, 1);
  _flags[flag].set(lv_switch_create(row));
  lv_obj_set_size(_flags[flag].get(), 44, 24);
  lv_obj_add_event_cb(_flags[flag].get(), event, LV_EVENT_VALUE_CHANGED, this);
}
void SoundSettingsScreen::steps(unsigned index, const char *text) {
  label(_body.get(), text);
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, SC(34));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  _stepButtons[index][0].set(button(row, index == 2 ? LV_SYMBOL_MINUS : "-30m", SC(50)));
  _values[index].set(label(row, ""));
  lv_obj_set_width(_values[index].get(), LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(_values[index].get(), &font16(), LV_PART_MAIN);
  _stepButtons[index][1].set(button(row, index == 2 ? LV_SYMBOL_PLUS : "+30m", SC(50)));
  for (auto &step : _stepButtons[index])
    lv_obj_add_event_cb(step.get(), event, LV_EVENT_CLICKED, this);
}
void SoundSettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
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
  lv_obj_set_style_pad_row(body, SC(8), LV_PART_MAIN);
  const auto caps = _settings.capabilities();
  if (caps.indicator) {
    toggle(SoundSettings::Indicator, TR("Incoming message blink"));
    for (unsigned row = 0; row < 2; ++row) {
      label(body, row ? TR("Direct message color") : TR("Room / channel color"));
      auto *container = lv_obj_create(body);
      lv_obj_remove_style_all(container);
      lv_obj_set_size(container, width - 4, SC(26));
      lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(container, 3, LV_PART_MAIN);
      for (unsigned i = 0; i < notification::ColorCount; ++i) {
        auto *swatch = lv_btn_create(container);
        _swatches[row][i].set(swatch);
        lv_obj_set_size(swatch, (width - 4 - 3 * (notification::ColorCount - 1)) / notification::ColorCount,
                        SC(26));
        lv_obj_set_style_radius(swatch, 4, LV_PART_MAIN);
        for (auto state :
             {lv_style_selector_t(LV_PART_MAIN), lv_style_selector_t(LV_PART_MAIN | LV_STATE_FOCUSED),
              lv_style_selector_t(LV_PART_MAIN | LV_STATE_FOCUS_KEY),
              lv_style_selector_t(LV_PART_MAIN | LV_STATE_PRESSED)}) {
          lv_obj_set_style_bg_color(swatch, lv_color_hex(notification::colorRgb(i)), state);
          lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, state);
        }
        lv_obj_add_event_cb(swatch, event, LV_EVENT_CLICKED, this);
      }
    }
  }
  if (caps.sound) {
    toggle(SoundSettings::Master, TR("Sound"));
    if (caps.loud) {
      toggle(SoundSettings::Loud, TR("Loud alerts"));
      label(body, TR("higher-pitched chime; louder on most buzzers"));
    }
    toggle(SoundSettings::Messages, TR("Message sound"));
    toggle(SoundSettings::Direct, TR("DM sound"));
    toggle(SoundSettings::Mentions, TR("@ mention sound"));
    toggle(SoundSettings::Dnd, TR("Do Not Disturb"));
    steps(0, TR("Start time"));
    steps(1, TR("End time"));
    if (caps.files) {
      const char *names[] = {TR("Message sound"), TR("Direct (DM) sound"), TR("@ mention sound")};
      for (unsigned i = 0; i < SoundSettings::SlotCount; ++i) {
        label(body, names[i]);
        _files[i].set(button(body, "", width - 4));
        _fileLabels[i].set(lv_obj_get_child(_files[i].get(), 0));
        lv_obj_add_event_cb(_files[i].get(), event, LV_EVENT_CLICKED, this);
      }
    }
    if (caps.volume)
      steps(2, TR("Volume"));
  }
  refresh();
}
void SoundSettingsScreen::refresh() {
  if (!_body.get())
    return;
  const auto generation = _generation;
  const auto state = _settings.read();
  if (_generation != generation || !_body.get())
    return;
  for (unsigned i = 0; i < SoundSettings::FlagCount; ++i) {
    auto *flag = _flags[i].get();
    if (!flag)
      continue;
    if (state.flags[i])
      lv_obj_add_state(flag, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(flag, LV_STATE_CHECKED);
  }
  for (unsigned i = 0; i < 3; ++i) {
    char text[96];
    if (_values[i].get()) {
      if (i == 2)
        snprintf(text, sizeof text, "%u%%", unsigned(state.volume));
      else
        notification::formatSlot(i ? state.end : state.start, text, sizeof text);
      lv_label_set_text(_values[i].get(), text);
    }
    if (_fileLabels[i].get()) {
      _settings.fileName(i, text, sizeof text);
      lv_label_set_text(_fileLabels[i].get(), text[0] ? text : TR("Built-in"));
    }
  }
  for (unsigned row = 0; row < 2; ++row)
    for (unsigned i = 0; i < notification::ColorCount; ++i) {
      auto *swatch = _swatches[row][i].get();
      if (!swatch)
        continue;
      lv_obj_set_style_border_width(swatch, i == state.colors[row] ? 2 : 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(
          swatch, lv_color_hex(i == state.colors[row] ? 0xffffff : colors().COLOR_BORDER), LV_PART_MAIN);
    }
}
void SoundSettingsScreen::event(lv_event_t *event) {
  auto &self = *static_cast<SoundSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  const auto generation = self._generation;
  auto *target = lv_event_get_target(event);
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    for (unsigned i = 0; i < SoundSettings::FlagCount; ++i) {
      if (target != self._flags[i].get())
        continue;
      const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
      const bool saved =
          self._settings.setFlag(static_cast<SoundSettings::Flag>(i), on, platform::milliseconds());
      if (self._generation != generation || !self._body.get())
        return;
      self.refresh();
      if (self._generation != generation || !self._body.get())
        return;
      if (!saved) {
        self.notify(TR("Save failed"), 1800);
        return;
      }
      if (i == SoundSettings::Master)
        self.notify(on ? TR("Sound on") : TR("Sound off"), 900);
      if (i == SoundSettings::Loud)
        self.notify(on ? TR("Loud alerts: on") : TR("Loud alerts: off"), 1100);
      if (self._generation != generation || !self._body.get())
        return;
      if (i == SoundSettings::Loud || (on && i <= SoundSettings::Mentions))
        self.preview(i == SoundSettings::Direct     ? 1
                     : i == SoundSettings::Mentions ? 2
                                                    : 0,
                     i != SoundSettings::Master && i != SoundSettings::Loud);
      return;
    }
  }
  if (code != LV_EVENT_CLICKED)
    return;
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned step = 0; step < 2; ++step) {
      if (target != self._stepButtons[i][step].get())
        continue;
      if (i == 2)
        self._settings.stepVolume(step ? 1 : -1);
      else
        self._settings.stepTime(i == 1, step ? 1 : -1);
      if (self._generation != generation || !self._body.get())
        return;
      self.refresh();
      if (i == 2 && self._generation == generation && self._body.get())
        self.preview(0, false);
      return;
    }
    if (target == self._files[i].get()) {
      self.openMenu(i);
      return;
    }
  }
  for (unsigned row = 0; row < 2; ++row)
    for (unsigned i = 0; i < notification::ColorCount; ++i)
      if (target == self._swatches[row][i].get()) {
        const bool saved = self._settings.setColor(row, i);
        self.refresh();
        if (!saved && self._generation == generation && self._body.get())
          self.notify(TR("Save failed"), 1800);
        return;
      }
}
void SoundSettingsScreen::openMenu(unsigned slot) {
  if (slot >= SoundSettings::SlotCount || !_settings.capabilities().files)
    return;
  const auto generation = _generation, menuGeneration = _menuGeneration + 1;
  closeMenu();
  if (_generation != generation || !_body.get() || _menuGeneration != menuGeneration || menuOpen())
    return;
  const auto width = lv_disp_get_hor_res(nullptr), height = lv_disp_get_ver_res(nullptr);
  const auto top = _host.contentTop ? _host.contentTop() : 0;
  if (_generation != generation || !_body.get() || _menuGeneration != menuGeneration)
    return;
  auto *root = lv_obj_create(lv_layer_top());
  _menu.set(root);
  lv_obj_add_event_cb(root, menuDeleted, LV_EVENT_DELETE, this);
  _menuSlot = slot;
  lv_obj_remove_style_all(root);
  const lv_coord_t popupWidth = width * 82 / 100;
  lv_obj_set_size(root, popupWidth, 168);
  lv_obj_set_pos(root, (width - popupWidth) / 2, top + (height - top - 168) / 2);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 8, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  const char *names[] = {"Message", "Direct (DM)", "@ mention"};
  auto *title = lv_label_create(root);
  lv_label_set_text_fmt(title, "%s sound", names[slot]);
  lv_obj_set_style_text_font(title, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 12, 12);
  _menuFiles.set(button(root, TR(LV_SYMBOL_DIRECTORY "  Choose .wav from files"), popupWidth - 24));
  _menuBuiltin.set(button(root, TR("Built-in (default)"), popupWidth - 24));
  lv_obj_set_size(_menuFiles.get(), popupWidth - 24, 40);
  lv_obj_set_pos(_menuFiles.get(), 12, 44);
  lv_obj_set_size(_menuBuiltin.get(), popupWidth - 24, 40);
  lv_obj_set_pos(_menuBuiltin.get(), 12, 92);
  _menuClose.set(button(root, LV_SYMBOL_CLOSE, 30));
  lv_obj_set_height(_menuClose.get(), 26);
  lv_obj_align(_menuClose.get(), LV_ALIGN_TOP_RIGHT, -6, 6);
  tanCloseRed(lv_obj_get_child(_menuClose.get(), 0));
  for (auto *object : {_menuFiles.get(), _menuBuiltin.get(), _menuClose.get()})
    lv_obj_add_event_cb(object, menuEvent, LV_EVENT_CLICKED, this);
  if (_host.focus)
    _host.focus(_menuFiles.get());
}
void SoundSettingsScreen::menuEvent(lv_event_t *event) {
  auto &self = *static_cast<SoundSettingsScreen *>(lv_event_get_user_data(event));
  if (!self._menu.get() || !self._body.get())
    return;
  auto *target = lv_event_get_target(event);
  const bool builtin = target == self._menuBuiltin.get(), files = target == self._menuFiles.get();
  if (!builtin && !files && target != self._menuClose.get())
    return;
  const auto slot = self._menuSlot, generation = self._generation, menuGeneration = self._menuGeneration + 1;
  self.closeMenu();
  if (self._generation != generation || !self._body.get() || self._menuGeneration != menuGeneration ||
      self.menuOpen())
    return;
  if (builtin) {
    const bool saved = self._settings.useBuiltin(slot);
    self.refresh();
    if (self._generation != generation || !self._body.get())
      return;
    if (!saved)
      self.notify(TR("Save failed"), 1800);
    else
      self.preview(slot, true);
  } else if (files && self._settings.selectFileSlot(slot) && self._host.openFiles)
    self._host.openFiles(self._host.context);
}
} // namespace screens
} // namespace ui
