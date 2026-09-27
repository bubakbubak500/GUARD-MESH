// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/GpsStatus.h"
namespace ui {
class GpsSettings {
public:
  struct Host {
    void *context;
    void (*snapshot)(void *, gps::Snapshot &);
    void (*setEnabled)(void *, bool);
    uint32_t defaultBaud;
  };
  struct State {
    gps::Snapshot position;
    uint32_t baud;
    uint16_t privacy;
  };
  explicit GpsSettings(Host host) : _host(host) {}
  State read() const;
  void setEnabled(bool);
  bool setBaud(unsigned index);
  bool setPrivacy(unsigned index);
  static uint32_t baud(unsigned index);
  static uint16_t privacy(unsigned index);
  static constexpr unsigned BaudCount = 5, PrivacyCount = 4;
  void acquisition(bool enabled, uint32_t now) { _status.acquisition(enabled, now); }
  void format(const gps::Snapshot &, uint32_t now, bool compact, char *, size_t);
  void status(uint32_t now, bool compact, char *, size_t);

private:
  Host _host;
  gps::Status _status;
};
} // namespace ui
