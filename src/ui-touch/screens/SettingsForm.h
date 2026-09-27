// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
class UITask;
namespace ui { namespace screens {
struct SettingsFormHost {
  UITask* (*task)();
  void (*syncKeyboard)();
  void (*attachField)(lv_obj_t*);
  bool (*deleting)(lv_event_t*);
  void (*refresh)();
  void (*filterText)(const lv_font_t*, char*, size_t, const char*);
  lv_event_cb_t copyLabel;
  void (*share)();
  void (*exportBackup)(const char*);
  void (*importBackup)();
  lv_event_cb_t clampDropdown, openAdvert;
  lv_event_cb_t antennaSelect, femChanged;
  void (*openRegions)();
  void (*closeModal)();
  void (*attachSymbols)(lv_obj_t*);
};
// The owner disconnects its callbacks before replacing a body. External parent
// deletion invalidates it before any child can auto-save during destruction.
class SettingsForm {
public:
  explicit SettingsForm(SettingsFormHost host) : _host(host) {}
  ~SettingsForm();
  SettingsForm(const SettingsForm&) = delete;
  SettingsForm& operator=(const SettingsForm&) = delete;
protected:
  void adopt(lv_obj_t* body, lv_coord_t width);
  bool accepts(lv_event_t*) const;
  int settingsRowLabel(lv_obj_t*, int, int, const char*, uint32_t, const lv_font_t*, int);
  SettingsFormHost _host;
  lv_coord_t _width = 0;
private:
  lv_obj_t* _body = nullptr;
  static void deleted(lv_event_t*);
  void detach(lv_obj_t*);
};
} }
