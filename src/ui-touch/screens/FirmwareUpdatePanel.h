// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/FirmwareUpdateJobs.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
// About presents the fork's release source and manual installation instructions.
// Old install calls are rejected; completed legacy jobs are drained safely.
class FirmwareUpdatePanel {
public:
  struct Host {
    void *context;
    void (*alert)(void *, const char *, int);
  };
  struct Options {
    const char *firmware;
    bool ota;
  };
  FirmwareUpdatePanel(FirmwareUpdateJobs &jobs, Host host) : _jobs(jobs), _host(host) {}
  ~FirmwareUpdatePanel() { detach(); }
  FirmwareUpdatePanel(const FirmwareUpdatePanel &) = delete;
  FirmwareUpdatePanel &operator=(const FirmwareUpdatePanel &) = delete;
  void build(lv_obj_t *body, lv_coord_t width, Options options);
  void detach();
  void poll(uint32_t now);
  void install(int version, bool beta);

private:
  lv_obj_t *label(lv_coord_t width, const char *text);
  FirmwareUpdateJobs &_jobs;
  Host _host;
  widgets::ObjectRef _body;
};
} // namespace screens
} // namespace ui
