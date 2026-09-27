// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/KeyboardSettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class KeyboardSettingsScreen {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
    void (*flagApplied)(void *, KeyboardSettings::Flag, bool, lv_obj_t *);
  };
  KeyboardSettingsScreen(KeyboardSettings &settings, Host host) : _settings(settings), _host(host) {}
  ~KeyboardSettingsScreen() { detach(); }
  KeyboardSettingsScreen(const KeyboardSettingsScreen &) = delete;
  KeyboardSettingsScreen &operator=(const KeyboardSettingsScreen &) = delete;
  void build(lv_obj_t *body, lv_coord_t width);
  void detach();
  void refresh();
  bool capturing() const { return _body.get() && _capture >= 0; }
  bool captureKey(int key);

private:
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  void unbind(lv_obj_t *);
  void notify(const char *, int);
  void flagNotice(KeyboardSettings::Flag, bool);
  lv_obj_t *label(lv_obj_t *, const char *);
  lv_obj_t *toggle(const char *);
  void flag(KeyboardSettings::Flag, const char *, const char *hint = nullptr);
  void binding(unsigned index);
  KeyboardSettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _light, _slider, _mode, _modeLabel;
  widgets::ObjectRef _presets[5], _flags[KeyboardSettings::FlagCount], _layouts[KEYBOARD_LAYOUT_COUNT];
  widgets::ObjectRef _bindings[KeyBindings::Count], _keyLabels[KeyBindings::Count];
  lv_coord_t _width = 0;
  uint32_t _generation = 0;
  int _capture = -1;
};
} // namespace screens
} // namespace ui
