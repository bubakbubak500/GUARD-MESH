// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/BatterySettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class BatterySettingsScreen {
public:
  struct Host {
    void *context;
    void (*history)(void *);
    void (*alert)(void *, const char *, int);
  };
  BatterySettingsScreen(BatterySettings &settings, Host host) : _settings(settings), _host(host) {}
  ~BatterySettingsScreen() { detach(); }
  BatterySettingsScreen(const BatterySettingsScreen &) = delete;
  BatterySettingsScreen &operator=(const BatterySettingsScreen &) = delete;
  void build(lv_obj_t *body, lv_coord_t width);
  void detach();
  void refresh();

private:
  static void event(lv_event_t *);
  static void deleted(lv_event_t *);
  void notify(const char *, int);
  void unbind(lv_obj_t *);
  BatterySettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _history, _calibrate, _sleep;
  uint32_t _generation = 0, _pending = 0;
};
} // namespace screens
} // namespace ui
