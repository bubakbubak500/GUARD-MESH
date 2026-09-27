// SPDX-License-Identifier: GPL-3.0-or-later
#include "KeyboardSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui {
void KeyboardSettings::load() {
  if (_loaded)
    return;
  _loaded = true;
  _lightMode = touchPrefsGetKbBacklight();
  if (_lightMode > 2)
    _lightMode = 2;
  _brightness = touchPrefsGetKbdBacklight();
  if (_brightness > 100)
    _brightness = 100;
  if (!_brightness && _host.capabilities.light == Light::Brightness)
    _brightness = 1;
  for (unsigned i = 0; i < KeyBindings::Count; ++i)
    _bindings.restore(i, i < KeyBindings::Tabs ? touchPrefsGetNavKey(i)
                                               : touchPrefsGetNavDirKey(i - KeyBindings::Tabs));
}
KeyboardSettings::State KeyboardSettings::read() {
  load();
  State state;
  state.flags[Accent] = touchPrefsGetAccentPopups();
  state.flags[EnterSends] = touchPrefsGetEnterSends();
  state.flags[Flash] = touchPrefsGetMsgFlash();
  state.flags[Legacy] = touchPrefsGetKbForceLegacy();
  state.flags[KeyboardNav] = touchPrefsGetKbdNav();
  state.flags[TrackballNav] = touchPrefsGetTbNav();
  state.flags[MenuLetters] = touchPrefsGetNavMenubarKeys();
  state.flags[Reverse] = touchPrefsGetScrollReverse();
  state.flags[Edge] = touchPrefsGetEdgeScroll();
  state.layouts = touchPrefsGetEnabledLayouts();
  state.lightMode = _lightMode;
  state.brightness = _brightness;
  for (unsigned i = 0; i < KeyBindings::Count; ++i)
    state.keys[i] = _bindings.key(i);
  return state;
}
bool KeyboardSettings::supports(Flag flag) const {
  switch (flag) {
  case Accent:
    return true;
  case EnterSends:
  case Flash:
    return _host.capabilities.enterFlash;
  case Legacy:
    return _host.capabilities.legacy;
  case KeyboardNav:
  case TrackballNav:
  case MenuLetters:
    return _host.capabilities.navigation == Navigation::All;
  case Reverse:
  case Edge:
    return _host.capabilities.trackball;
  default:
    return false;
  }
}
bool KeyboardSettings::setFlag(Flag flag, bool on) {
  if (!supports(flag))
    return false;
  bool saved = false;
  switch (flag) {
  case Accent:
    saved = touchPrefsSetAccentPopups(on);
    break;
  case EnterSends:
    touchPrefsSetEnterSends(on);
    saved = true;
    break;
  case Flash:
    saved = touchPrefsSetMsgFlash(on);
    break;
  case Legacy:
    saved = touchPrefsSetKbForceLegacy(on);
    break;
  case KeyboardNav:
    saved = touchPrefsSetKbdNav(on);
    break;
  case TrackballNav:
    saved = touchPrefsSetTbNav(on);
    break;
  case MenuLetters:
    touchPrefsSetNavMenubarKeys(on);
    saved = true;
    break;
  case Reverse:
    touchPrefsSetScrollReverse(on);
    saved = true;
    break;
  case Edge:
    touchPrefsSetEdgeScroll(on);
    saved = true;
    break;
  default:
    break;
  }
  if (_host.flagChanged)
    _host.flagChanged(_host.context, flag, on);
  return saved;
}
const char *KeyboardSettings::layoutName(unsigned index) {
  static const char *names[] = {
      "English", "Bulgarian", "Russian", "Ukrainian", "Serbian", "Greek",    "Arabic (experimental)",
      "French",  "Dutch",     "German",  "Spanish",   "Italian", "Romanian", "Latvian"};
  static_assert(sizeof names / sizeof names[0] == KEYBOARD_LAYOUT_COUNT,
                "Every layout needs a settings name");
  return index < KEYBOARD_LAYOUT_COUNT ? names[index] : "";
}
bool KeyboardSettings::setLayout(unsigned id, bool on) {
  static_assert(KEYBOARD_LAYOUT_COUNT <= 16, "Persisted layout mask is 16 bits");
  if (!id || id >= KEYBOARD_LAYOUT_COUNT)
    return false;
  uint16_t mask = touchPrefsGetEnabledLayouts();
  if (on)
    mask |= uint16_t(1u << id);
  else
    mask &= uint16_t(~(1u << id));
  const bool saved = touchPrefsSetEnabledLayouts(mask);
  keyboardLayoutsSetEnabledMask(mask);
  const bool currentSaved = touchPrefsSetKeyboardLayout(static_cast<uint8_t>(keyboardLayoutsGetCurrent()));
  if (_host.layoutsChanged)
    _host.layoutsChanged(_host.context);
  return saved && currentSaved;
}
bool KeyboardSettings::supportsBinding(unsigned index) const {
  if (index >= KeyBindings::Count)
    return false;
  if (_host.capabilities.navigation == Navigation::All)
    return true;
  return _host.capabilities.navigation == Navigation::ExtraDirections && index >= KeyBindings::Tabs &&
         index != 10;
}
KeyBindings::Result KeyboardSettings::assign(unsigned index, int key) {
  load();
  if (!supportsBinding(index))
    return KeyBindings::Result::OutOfRange;
  const auto result = _bindings.validate(index, key);
  if (result != KeyBindings::Result::Ok)
    return result;
  const auto value = static_cast<uint8_t>(KeyBindings::lower(key));
  const bool saved = index < KeyBindings::Tabs ? touchPrefsSetNavKey(index, value)
                                               : touchPrefsSetNavDirKey(index - KeyBindings::Tabs, value);
  _bindings.restore(index, value);
  if (_host.keysChanged)
    _host.keysChanged(_host.context);
  return saved ? KeyBindings::Result::Ok : KeyBindings::Result::SaveFailed;
}
void KeyboardSettings::lightChanged(uint32_t now) {
  _lightPending = true;
  _lightSaveAt = now + 1200u;
  if (_host.lightChanged)
    _host.lightChanged(_host.context, _lightMode, _brightness);
}
void KeyboardSettings::setBrightness(int percent, uint32_t now) {
  load();
  const int minimum = _host.capabilities.light == Light::Brightness ? 1 : 0;
  _brightness = percent < minimum ? minimum : percent > 100 ? 100 : percent;
  if (!_lightMode)
    _lightMode = 1;
  lightChanged(now);
}
void KeyboardSettings::lightOff(uint32_t now) {
  load();
  _lightMode = 0;
  lightChanged(now);
}
void KeyboardSettings::cycleLight(uint32_t now) {
  load();
  _lightMode = (_lightMode + 1) % 3;
  lightChanged(now);
}
bool KeyboardSettings::saveLight() {
  load();
  _lightPending = false;
  touchPrefsSetKbdBacklight(_brightness); // Existing brightness setter has no failure result.
  const bool mode = touchPrefsSetKbBacklight(_lightMode);
  _lightSaved = mode;
  return _lightSaved;
}
void KeyboardSettings::tick(uint32_t now) {
  if (_lightPending && int32_t(now - _lightSaveAt) >= 0)
    saveLight();
}
} // namespace ui
