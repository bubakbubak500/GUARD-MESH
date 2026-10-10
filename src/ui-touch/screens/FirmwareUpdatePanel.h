// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/FirmwareUpdateJobs.h"
#include "../widgets/ObjectRef.h"
namespace ui { namespace screens {
class FirmwareUpdatePanel {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
    bool (*connected)(void *);
    bool (*wifiEnabled)(void *);
    bool (*setWifi)(void *, bool);
    void (*openWifi)(void *);
    bool (*ensureWorker)(void *);
    void (*confirmInstall)(void *, const char *);
    void (*reboot)(void *);
  };
  struct Options { const char *firmware; bool ota; };
  FirmwareUpdatePanel(FirmwareUpdateJobs &jobs, Host host) : _jobs(jobs), _host(host) {}
  ~FirmwareUpdatePanel() { detach(); }
  FirmwareUpdatePanel(const FirmwareUpdatePanel &) = delete;
  FirmwareUpdatePanel &operator=(const FirmwareUpdatePanel &) = delete;
  void build(lv_obj_t *, lv_coord_t, Options);
  void detach();
  void poll(uint32_t);
  void checkLatest();
  void installLatest(); // Rechecks the pinned, successfully resolved release.
  bool updateAvailable() const { return _available; }
private:
  lv_obj_t *label(lv_coord_t, const char *);
  lv_obj_t *button(lv_coord_t, const char *, lv_event_cb_t);
  void render();
  void status(const char *);
  bool connected() const { return _host.connected && _host.connected(_host.context); }
  static void removeCallbacks(lv_obj_t *, void *);
  FirmwareUpdateJobs &_jobs;
  Host _host;
  widgets::ObjectRef _body, _status, _check, _install, _wifi;
  FirmwareRelease _latest{};
  char _firmware[64]{}, _message[160]{};
  uint32_t _generation = 0, _rebootAt = 0;
  bool _ota = false, _available = false, _rebootPending = false;
};
} } // namespace ui::screens
