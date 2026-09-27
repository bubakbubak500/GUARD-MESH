// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/DisplaySettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class DisplaySettingsScreen {
public:
  struct Host {
    void *context;
    void (*attachField)(void *, lv_obj_t *);
    void (*syncField)(void *);
    void (*alert)(void *, const char *, int);
    void (*accentPicker)(void *);
    lv_event_cb_t clampDropdown;
    bool (*deleting)(lv_event_t *);
  };
  DisplaySettingsScreen(DisplaySettings &settings, Host host) : _settings(settings), _host(host) {}
  ~DisplaySettingsScreen() { detach(); }
  DisplaySettingsScreen(const DisplaySettingsScreen &) = delete;
  DisplaySettingsScreen &operator=(const DisplaySettingsScreen &) = delete;
  void build(lv_obj_t *, lv_coord_t width);
  void detach();

private:
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  void unbind(lv_obj_t *);
  lv_obj_t *label(const char *);
  lv_obj_t *button(lv_obj_t *, const char *, lv_coord_t width);
  void flag(DisplaySettings::Flag, const char *, const DisplaySettings::State &);
  void notify(const char *, int = 1200);
  void flagNotice(DisplaySettings::Flag, bool);
  DisplaySettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _timeout, _size, _flags[DisplaySettings::FlagCount], _theme[2], _accent,
      _rotation;
  lv_coord_t _width = 0;
  uint32_t _generation = 0;
};
} // namespace screens
} // namespace ui
