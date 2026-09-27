// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/FirmwareUpdateJobs.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
// The About shell owns the tree. This controller owns all update bindings and
// consumes completed jobs even while detached, so closing About cannot lose a
// successful OTA reboot or leave a completed download occupying the executor.
class FirmwareUpdatePanel {
public:
  struct Host {
    void *context;
    bool (*ensureExecutor)(void *);
    bool (*networkConnected)(void *);
    bool (*otaAvailable)(void *);
    void (*alert)(void *, const char *, int);
    void (*reboot)(void *);
    void (*selectChannel)(void *, bool);
    void (*previousVersions)(void *, int, bool);
  };
  struct Options {
    const char *firmware;
    int currentVersion;
    bool channelControls, ota, sd, launcher;
  };
  struct Release {
    int latest;
    bool checked, beta;
  };
  FirmwareUpdatePanel(FirmwareUpdateJobs &jobs, Host host) : _jobs(jobs), _host(host) {}
  ~FirmwareUpdatePanel() { detach(); }
  FirmwareUpdatePanel(const FirmwareUpdatePanel &) = delete;
  FirmwareUpdatePanel &operator=(const FirmwareUpdatePanel &) = delete;
  void build(lv_obj_t *body, lv_coord_t width, Options options, Release release);
  void detach();
  void refresh(Release release);
  void poll(uint32_t now);
  void install(int version, bool beta);

private:
  static void event(lv_event_t *);
  void saveToSd();
  void buttons();
  void notify(const char *, int);
  void begin(FirmwareUpdateJobs::Destination, int, bool);
  void showProgress();
  void status(bool sd, const char *, bool warning = false);
  lv_obj_t *label(lv_coord_t width, const char *text);
  lv_obj_t *button(lv_coord_t width, lv_coord_t height, const char *text);
  FirmwareUpdateJobs &_jobs;
  Host _host;
  Options _options{};
  Release _release{-1, false, false};
  char _firmware[64]{};
  widgets::ObjectRef _body, _summary, _install, _caption, _previous, _beta, _sd;
  widgets::ObjectRef _otaStatus, _sdStatus;
  uint32_t _generation = 0, _nextPoll = 0;
  bool _scheduled = false;
};
} // namespace screens
} // namespace ui
