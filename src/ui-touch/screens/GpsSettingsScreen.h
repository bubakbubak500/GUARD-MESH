// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/GpsSettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class GpsSettingsScreen {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
    lv_event_cb_t clampDropdown;
  };
  GpsSettingsScreen(GpsSettings &settings, Host host) : _settings(settings), _host(host) {}
  ~GpsSettingsScreen() { detach(); }
  GpsSettingsScreen(const GpsSettingsScreen &) = delete;
  GpsSettingsScreen &operator=(const GpsSettingsScreen &) = delete;
  void build(lv_obj_t *body, lv_coord_t width, bool hardware);
  void detach();
  void refresh(uint32_t now);

private:
  void detachCallbacks(lv_obj_t *);
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  static void dropdownOpened(lv_event_t *);
  lv_obj_t *label(const char *);
  void paintPrivacy(uint16_t);
  void notify(const char *, int);
  GpsSettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _status, _enabled, _baud, _privacy[GpsSettings::PrivacyCount];
  lv_coord_t _width = 0;
  uint32_t _generation = 0, _readingGeneration = 0;
  bool _reading = false;
};
} // namespace screens
} // namespace ui
