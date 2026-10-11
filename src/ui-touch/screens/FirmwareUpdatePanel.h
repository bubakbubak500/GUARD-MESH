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
  struct Options {
    const char *firmware; bool ota; const char *target;
    Options(const char *value, bool enabled, const char *board = nullptr)
      : firmware(value), ota(enabled), target(board) {}
  };
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
  lv_obj_t *card(lv_obj_t *, lv_coord_t);
  lv_obj_t *label(lv_obj_t *, lv_coord_t, const char *, bool strong = false);
  lv_obj_t *button(lv_obj_t *, lv_coord_t, const char *, lv_event_cb_t, bool primary = false);
  void render();
  void status(const char *);
  bool connected() const { return _host.connected && _host.connected(_host.context); }
  static void removeCallbacks(lv_obj_t *, void *);
  FirmwareUpdateJobs &_jobs;
  Host _host;
  widgets::ObjectRef _body, _status, _check, _install, _wifi;
  widgets::ObjectRef _summary, _heading, _current, _version, _versionArrow, _image, _hash;
  widgets::ObjectRef _summaryDown, _summaryInfo, _summaryWarning;
  widgets::ObjectRef _network, _networkStatus, _wifiSettings, _actions;
  widgets::ObjectRef _progressCard, _progressTitle, _progressValue, _progressBar, _progressNote;
  widgets::ObjectRef _progressDown, _progressShield, _stageDots[3], _stageLabels[3];
  FirmwareRelease _latest{};
  char _firmware[64]{}, _target[48]{}, _message[160]{};
  uint32_t _generation = 0, _rebootAt = 0;
  bool _ota = false, _available = false, _rebootPending = false;
};
} } // namespace ui::screens
