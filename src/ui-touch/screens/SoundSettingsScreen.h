// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/SoundSettings.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class SoundSettingsScreen {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
    void (*openFiles)(void *);
    void (*closeRoot)(lv_obj_t **);
    lv_coord_t (*contentTop)();
    void (*focus)(lv_obj_t *);
  };
  SoundSettingsScreen(SoundSettings &settings, Host host) : _settings(settings), _host(host) {}
  ~SoundSettingsScreen() { detach(); }
  SoundSettingsScreen(const SoundSettingsScreen &) = delete;
  SoundSettingsScreen &operator=(const SoundSettingsScreen &) = delete;
  void build(lv_obj_t *body, lv_coord_t width);
  void detach();
  void refresh();
  void closeMenu();
  bool menuOpen() const { return _menu.get(); }

private:
  static void deleted(lv_event_t *);
  static void menuDeleted(lv_event_t *);
  static void event(lv_event_t *);
  static void menuEvent(lv_event_t *);
  void unbind(lv_obj_t *);
  void clearMenuRefs();
  void openMenu(unsigned slot);
  void notify(const char *, int);
  void preview(unsigned slot, bool announce);
  lv_obj_t *label(lv_obj_t *, const char *);
  lv_obj_t *button(lv_obj_t *, const char *, lv_coord_t width);
  void toggle(SoundSettings::Flag, const char *);
  void steps(unsigned row, const char *);
  SoundSettings &_settings;
  Host _host;
  widgets::ObjectRef _body, _flags[SoundSettings::FlagCount];
  widgets::ObjectRef _stepButtons[3][2], _values[3]; // start/end/volume
  widgets::ObjectRef _files[SoundSettings::SlotCount], _fileLabels[SoundSettings::SlotCount];
  widgets::ObjectRef _swatches[2][notification::ColorCount];
  widgets::ObjectRef _menu, _menuFiles, _menuBuiltin, _menuClose;
  uint32_t _generation = 0, _menuGeneration = 0;
  unsigned _menuSlot = 0;
  lv_coord_t _width = 0;
};
} // namespace screens
} // namespace ui
