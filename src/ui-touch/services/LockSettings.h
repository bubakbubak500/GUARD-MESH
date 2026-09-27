// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
class LockSettings {
public:
  struct Capabilities {
    bool wallpaper, color, autoLock;
  };
  struct Host {
    void *context;
    void (*lockChanged)(void *, bool);
    Capabilities capabilities;
  };
  struct State {
    bool autoLock;
    uint32_t color;
    char wallpaper[160];
  };
  explicit LockSettings(Host host) : _host(host) {}
  State read() const;
  Capabilities capabilities() const { return _host.capabilities; }
  bool setLocked(bool);
  bool setColor(unsigned index);
  static void wallpaperName(const char *, char *, size_t);

private:
  Host _host;
};
} // namespace ui
