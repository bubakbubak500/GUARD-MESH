// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/ColorChoice.h"
#include "../services/LockSettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class LockSettingsScreen {
public:
  struct Host {
    void *context;
    void (*openWallpaper)(void *);
    void (*alert)(void *, const char *, int);
  };
  LockSettingsScreen(LockSettings &settings, Host host) : _settings(settings), _host(host) {}
  ~LockSettingsScreen() { detach(); }
  LockSettingsScreen(const LockSettingsScreen &) = delete;
  LockSettingsScreen &operator=(const LockSettingsScreen &) = delete;
  void build(lv_obj_t *, lv_coord_t);
  void detach();
  void wallpaperChanged(const char *);

private:
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  void unbind(lv_obj_t *);
  LockSettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _wallpaper, _caption, _locked, _colors[colorChoice::LockCount];
  uint32_t _generation = 0;
};
} // namespace screens
} // namespace ui
