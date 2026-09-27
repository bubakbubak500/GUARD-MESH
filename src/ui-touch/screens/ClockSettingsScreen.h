// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/ClockSettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class ClockSettingsScreen {
public:
  struct Host {
    void *context;
    void (*attachField)(void *, lv_obj_t *);
    void (*syncKeyboard)(void *);
    void (*alert)(void *, const char *, int);
    void (*closeRoot)(lv_obj_t **);
    lv_coord_t (*contentTop)();
  };
  ClockSettingsScreen(ClockSettings &settings, Host host) : _settings(settings), _host(host) {}
  ~ClockSettingsScreen() { detach(); }
  ClockSettingsScreen(const ClockSettingsScreen &) = delete;
  ClockSettingsScreen &operator=(const ClockSettingsScreen &) = delete;
  void build(lv_obj_t *body, lv_coord_t width, bool bootSync);
  void detach();
  void closePicker();
  bool pickerOpen() const { return _picker.get() != nullptr; }

private:
  enum Control { Sync, Manual, Hour12, Boot, Open, Zone, Minus, Plus, Count };
  static void event(lv_event_t *);
  static void deleted(lv_event_t *);
  static void picked(lv_event_t *);
  void detachCallbacks(lv_obj_t *);
  void openPicker();
  void refreshOffset();
  void notify(const char *, int);
  lv_obj_t *label(const char *text);
  lv_obj_t *button(Control, const char *);
  void toggle(Control, const char *, bool);
  ClockSettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _picker, _field, _zoneCaption, _offset;
  widgets::ObjectRef _controls[Count];
  lv_coord_t _width = 0;
  uint32_t _generation = 0, _pickerGeneration = 0;
};
} // namespace screens
} // namespace ui
