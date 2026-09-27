// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/GeneralSettings.h"
#include "../widgets/ObjectRef.h"
#include "ConfirmDialog.h"
namespace ui {
namespace screens {
class GeneralSettingsScreen {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
    lv_event_cb_t clampDropdown;
    ConfirmDialog::Host confirmation;
  };
  GeneralSettingsScreen(GeneralSettings &settings, Host host)
      : _settings(settings), _host(host), _confirmation(host.confirmation) {}
  ~GeneralSettingsScreen() {
    _destroying = true;
    detach();
  }
  GeneralSettingsScreen(const GeneralSettingsScreen &) = delete;
  GeneralSettingsScreen &operator=(const GeneralSettingsScreen &) = delete;
  void build(lv_obj_t *, lv_coord_t);
  void detach();
  bool confirmationOpen() const { return _confirmation.isOpen(); }
  void dismissConfirmation() {
    ++_request;
    _confirmation.dismiss();
  }

private:
  enum Control { Advert, History, Fallback, Sd, Recovery, Console, Setup, Reboot, Count };
  enum class Confirmation { Unlimited, Recovery };
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  void unbind(lv_obj_t *);
  void confirm(Confirmation);
  void notify(const char *, int = 1200);
  lv_obj_t *label(const char *);
  void button(Control, const char *);
  void toggle(Control, const char *, bool);
  void dropdown(Control, const char *, unsigned);
  void storage(GeneralSettings::Storage);
  GeneralSettings &_settings;
  Host _host;
  ConfirmDialog _confirmation;
  widgets::ObjectRef _body, _controls[Count], _storage;
  lv_coord_t _width = 0;
  uint32_t _generation = 0, _request = 0;
  bool _destroying = false;
};
} // namespace screens
} // namespace ui
