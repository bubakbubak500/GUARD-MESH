// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/SystemDiagnostics.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
// The settings shell owns the body. This controller owns its label bindings and
// refresh policy, and never retains a widget after DELETE or controller teardown.
class SystemInfoScreen {
public:
  struct Host {
    void *context;
    void (*read)(void *, bool details, diagnostics::Snapshot &);
  };
  explicit SystemInfoScreen(Host host) : _host(host) {}
  ~SystemInfoScreen() { detach(); }
  SystemInfoScreen(const SystemInfoScreen &) = delete;
  SystemInfoScreen &operator=(const SystemInfoScreen &) = delete;
  void build(lv_obj_t *body, bool memoryOnly = false);
  void detach();
  void refresh(uint32_t now);

private:
  void render(bool first);
  Host _host;
  widgets::ObjectRef _body, _live, _rest;
  uint32_t _generation = 0, _next = 0, _readingGeneration = 0;
  bool _memory = false, _scheduled = false, _reading = false;
};
} // namespace screens
} // namespace ui
