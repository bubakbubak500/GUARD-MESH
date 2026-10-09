// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneralSettingsScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void GeneralSettingsScreen::unbind(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    unbind(lv_obj_get_child(object, i));
}
void GeneralSettingsScreen::detach() {
  ++_generation;
  unbind(_body.get());
  _body.set(nullptr);
  _storage.set(nullptr);
  _heardCount.set(nullptr);
  auto *history = _controls[History].get(), *fallback = _controls[Fallback].get();
  for (auto &object : _controls) {
    unbind(object.get());
    object.set(nullptr);
  }
  if (history)
    lv_dropdown_close(history);
  if (fallback)
    lv_dropdown_close(fallback);
  dismissConfirmation();
}
void GeneralSettingsScreen::deleted(lv_event_t *e) {
  static_cast<GeneralSettingsScreen *>(lv_event_get_user_data(e))->detach();
}
void GeneralSettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
lv_obj_t *GeneralSettingsScreen::label(const char *text) {
  auto *object = lv_label_create(_body.get());
  lv_obj_set_width(object, _width - 4);
  lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  return object;
}
void GeneralSettingsScreen::button(Control id, const char *text) {
  auto *object = lv_btn_create(_body.get());
  _controls[id].set(object);
  lv_obj_set_width(object, _width - 4);
  lv_obj_set_height(object, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(object, SC(34), LV_PART_MAIN);
  styleButton(object);
  auto *caption = lv_label_create(object);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_obj_set_width(caption, lv_pct(100));
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(caption);
  if (id == Reboot) {
    lv_obj_set_style_bg_color(object, lv_color_hex(themeRole(0xC44B55, colors().COLOR_STATUS_DANGER)),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, lv_color_hex(themeRole(0xA13F47, colors().COLOR_STATUS_DANGER_PRESSED)),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_ON_STATUS_DANGER), LV_PART_MAIN);
  }
  lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
}
void GeneralSettingsScreen::toggle(Control id, const char *text, bool on) {
  auto *row = lv_obj_create(_body.get());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, _width - 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  auto *caption = lv_label_create(row);
  useChainedFont(caption);
  lv_label_set_text(caption, text);
  lv_obj_set_flex_grow(caption, 1);
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  auto *object = lv_switch_create(row);
  _controls[id].set(object);
  lv_obj_set_size(object, 44, 24);
  if (on)
    lv_obj_add_state(object, LV_STATE_CHECKED);
  lv_obj_add_event_cb(object, event, LV_EVENT_VALUE_CHANGED, this);
}
void GeneralSettingsScreen::dropdown(Control id, const char *options, unsigned selection) {
  auto *object = lv_dropdown_create(_body.get());
  _controls[id].set(object);
  lv_dropdown_set_options(object, options);
  lv_dropdown_set_selected(object, selection);
  lv_obj_set_width(object, _width - 4);
  lv_obj_add_event_cb(object, event, LV_EVENT_ALL, this);
}
void GeneralSettingsScreen::storage(GeneralSettings::Storage status) {
  if (!_storage.get())
    return;
  const char *text = "";
  uint32_t color = colors().COLOR_SUB;
  switch (status) {
  case GeneralSettings::Storage::Internal:
    text = TR("Contacts are on internal flash.");
    break;
  case GeneralSettings::Storage::Sd:
    text = TR("Contacts are saved to the SD card.");
    color = colors().COLOR_STATUS_OK_TEXT;
    break;
  case GeneralSettings::Storage::MissingCard:
    text = TR("SD data is unavailable - contacts and channels cannot be saved until the card is reinserted.");
    color = lightSurfaceTextRgb(0xE34B4B);
    break;
  case GeneralSettings::Storage::MissingFullData:
    text = TR("SD data is unavailable - identity, settings, contacts and channels cannot be saved until the "
              "card is reinserted.");
    color = lightSurfaceTextRgb(0xE34B4B);
    break;
  case GeneralSettings::Storage::MigrationBlocked:
    text = TR("SD data migration is incomplete. Identity and settings remain internal; use Copy internal "
              "data to SD to retry.");
    color = lightSurfaceTextRgb(0xE3A127);
    break;
  case GeneralSettings::Storage::MountFailed:
    text = TR(
        "Contacts are on internal flash - the SD card did not mount at boot. Re-seat the card and reboot.");
    color = lightSurfaceTextRgb(0xE3A127);
    break;
  }
  lv_label_set_text(_storage.get(), text);
  lv_obj_set_style_text_color(_storage.get(), lv_color_hex(color), LV_PART_MAIN);
}
void GeneralSettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  if (_destroying)
    return;
  const bool reuse = body && body == _body.get();
  const auto expected = _generation + 1;
  detach();
  if (!body || _generation != expected)
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  _width = width;
  const auto state = _settings.read();
  if (_generation != expected || !_body.get())
    return;
  const auto caps = _settings.capabilities();
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(10), LV_PART_MAIN);
  button(Advert, TR("Send advert now"));
  unsigned heardCount = 0, heardCapacity = 0;
  _settings.heardNames(heardCount, heardCapacity);
  if (heardCapacity) {
    char text[96];
    snprintf(text, sizeof text, TR("Heard names: %u / %u nodes"), heardCount, heardCapacity);
    _heardCount.set(label(text));
    button(HeardNames, TR("Clear heard-name cache"));
  }
  label(TR("Keep per chat (messages)"));
  dropdown(History, TR("100\n250\n500\n1000\n2000\nNo limit"), state.history);
  label(TR("Older messages in a chat are dropped past this. A very large or unlimited history makes the chat "
           "list slower."));
  label(TR("Chat save fallback (failed tries)"));
  dropdown(Fallback, TR("Off\n1\n2\n3\n5\n8"), state.fallback);
  if (caps.sd)
    toggle(Sd, TR("Store data on SD (reboot)"), state.sd);
  if (caps.storageStatus) {
    _storage.set(label(""));
    storage(state.storage);
  }
  if (caps.recovery)
    button(Recovery, TR(LV_SYMBOL_DOWNLOAD "  Copy internal data to SD"));
  if (caps.console) {
    label(TR("Console mode"));
    toggle(Console, TR("Text console (experimental)"), state.console);
    label(
        TR("EXPERIMENTAL. Boots into a text console with no graphical interface: type commands, read "
           "replies. Frees the memory and processor time the interface uses. Type 'ui' in the console to "
           "come back, and if it ever fails to start the device returns here on its own. Toggling reboots."));
  }
  char text[96];
  snprintf(text, sizeof text, LV_SYMBOL_REFRESH "  %s", TR("Run setup again"));
  button(Setup, text);
  button(Reboot, TR("Reboot device"));
}
void GeneralSettingsScreen::confirm(Confirmation kind) {
  const auto generation = _generation;
  const auto request = ++_request;
  const bool resume = _settings.capabilities().resumeRecovery;
  const char *text =
      kind == Confirmation::HeardNames
          ? TR("Clear remembered node names?\n\nContacts and Found stay unchanged. Names are learned again from new adverts.")
      : kind == Confirmation::Unlimited
          ? TR("Turn the history limit off?\n\nA busy channel can then fill the whole message store, which "
               "uses more space and makes the chat list and app noticeably slower. Only do this if you "
               "really need the full backlog.")
      : resume ? TR("Copy only missing internal files to a matching SD profile,\nkeep existing SD data, then "
                    "reboot?")
               : TR("Overwrite the SD card's settings and\nidentity with the internal copies,\nthen reboot?");
  _confirmation.showCaptured(
      text,
      kind == Confirmation::HeardNames ? TR("Clear")
      : kind == Confirmation::Unlimited ? TR("Turn off")
      : resume                        ? TR("Resume")
                                      : TR("Copy"),
      [this, generation, request, kind] {
        if (_generation != generation || _request != request || !_body.get())
          return;
        if (kind == Confirmation::HeardNames) {
          const bool queued = _settings.clearHeardNames();
          if (_generation != generation || !_body.get()) return;
          unsigned count = 0, capacity = 0;
          _settings.heardNames(count, capacity);
          char text[96];
          snprintf(text, sizeof text, TR("Heard names: %u / %u nodes"), count, capacity);
          if (_heardCount.get()) lv_label_set_text(_heardCount.get(), text);
          notify(queued ? TR("Clearing heard-name cache...") : TR("Save failed"));
          return;
        }
        if (kind == Confirmation::Recovery) {
          _settings.action(GeneralSettings::Action::Recover);
          return;
        }
        const bool saved = _settings.setHistory(5);
        if (_generation != generation || !_body.get())
          return;
        if (_controls[History].get())
          lv_dropdown_set_selected(_controls[History].get(), 5);
        notify(saved ? TR("History limit off") : TR("Save failed"));
      },
      false);
}
void GeneralSettingsScreen::event(lv_event_t *e) {
  auto &self = *static_cast<GeneralSettingsScreen *>(lv_event_get_user_data(e));
  if (!self._body.get())
    return;
  const auto generation = self._generation;
  auto *target = lv_event_get_target(e);
  const auto code = lv_event_get_code(e);
  unsigned id = Count;
  for (unsigned i = 0; i < Count; ++i)
    if (target == self._controls[i].get()) {
      id = i;
      break;
    }
  if (id == Count)
    return;
  if (id == History || id == Fallback) {
    if (code == LV_EVENT_CLICKED && self._host.clampDropdown) {
      self._host.clampDropdown(e);
      return;
    }
    if (code != LV_EVENT_VALUE_CHANGED)
      return;
    const unsigned selected = lv_dropdown_get_selected(target);
    if (id == History) {
      self.dismissConfirmation();
      if (generation != self._generation || !self._body.get())
        return;
    }
    if (id == History && selected == 5) {
      const auto state = self._settings.read();
      if (generation != self._generation || !self._body.get())
        return;
      lv_dropdown_set_selected(target, state.history);
      self.confirm(Confirmation::Unlimited);
      return;
    }
    const bool saved =
        id == History ? self._settings.setHistory(selected) : self._settings.setFallback(selected);
    if (!saved && generation == self._generation && self._body.get())
      self.notify(TR("Save failed"));
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    const bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
    if (id == Sd) {
      const bool saved = self._settings.setSd(on);
      const auto state = self._settings.read();
      if (generation != self._generation || !self._body.get())
        return;
      self.storage(state.storage);
      self.notify(saved ? on ? TR("Data -> SD card on reboot\n(card must be inserted)")
                             : TR("Data -> internal on reboot")
                        : TR("Save failed"),
                  1800);
    } else if (id == Console) {
      const bool saved = self._settings.setConsole(on);
      if (!saved && generation == self._generation && self._body.get())
        self.notify(TR("Save failed"));
    }
    return;
  }
  if (code != LV_EVENT_CLICKED)
    return;
  if (id == Advert) {
    const bool sent = self._settings.advert();
    if (generation == self._generation && self._body.get())
      self.notify(sent ? TR("Advert sent") : TR("Advert failed"), 900);
  } else if (id == HeardNames)
    self.confirm(Confirmation::HeardNames);
  else if (id == Recovery)
    self.confirm(Confirmation::Recovery);
  else if (id == Setup)
    self._settings.action(GeneralSettings::Action::Setup);
  else if (id == Reboot)
    self._settings.action(GeneralSettings::Action::Reboot);
}
} // namespace screens
} // namespace ui
