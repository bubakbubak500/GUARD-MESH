// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/BatteryHistory.h"
namespace fs {
class FS;
}
namespace ui {
// Synchronous UI-thread service. Resolves one backend for each entire operation;
// publication keeps the old log until staging and readback both succeed.
class BatteryHistory {
public:
  struct Backend {
    fs::FS *filesystem = nullptr;
    char root[32]{};
    bool sd = false;
  };
  struct Host {
    void *context;
    Backend (*resolve)(void *);
    void (*failure)(void *, const Backend &);
    battery::Sample (*sample)(void *);
  };
  enum class Result { Ok, NoStorage, InvalidData, ReadFailed, WriteFailed, CommitFailed, RecoveryRequired };
  explicit BatteryHistory(Host host) : _host(host) {}
  bool due(uint32_t now) const { return !_scheduled || int32_t(now - _next) >= 0; }
  void tick(uint32_t now);
  Result append(const battery::Sample &);
  Backend resolve() const { return _host.resolve ? _host.resolve(_host.context) : Backend{}; }
  Result load(battery::Sample *, size_t capacity, size_t &count);
  Result load(const Backend &, battery::Sample *, size_t capacity, size_t &count);
  Result clear();
  Result clear(const Backend &);
  Result lastResult() const { return _last; }

private:
  Result finish(Result, const Backend &);
  Host _host;
  Result _last = Result::Ok;
  uint32_t _next = 0;
  bool _scheduled = false;
};
} // namespace ui
