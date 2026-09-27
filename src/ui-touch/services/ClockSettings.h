// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/ClockTime.h"
#include <stddef.h>
namespace ui {
class ClockSettings {
public:
  struct Host {
    void *context;
    uint32_t (*now)(void *);
    void (*setTime)(void *, uint32_t);
    bool (*sync)(void *);
    uint32_t minimumEpoch;
  };
  enum class Flag { Hour12, BootWifi, OpenWifi };
  struct State {
    int zone, offset;
    bool hour12, bootWifi, openWifi;
  };
  explicit ClockSettings(Host host) : _host(host) {}
  State read() const;
  int zoneCount() const;
  const char *zoneLabel(int) const;
  void setZone(int);
  int stepOffset(int);
  void setFlag(Flag, bool);
  void prefill(char *, size_t) const;
  clock::ParseResult setManual(const char *);
  bool sync() { return _host.sync && _host.sync(_host.context); }

private:
  void applyTimezone();
  Host _host;
};
} // namespace ui
