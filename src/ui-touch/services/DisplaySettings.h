// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
class DisplaySettings {
public:
  enum Flag { Miles, Colorful, Compact, HideName, Glance, GlanceLocked, MessageLed, Sensors, FlagCount };
  enum class Restart { Night, Day, Rotation };
  enum class Result { Applied, Unchanged, Invalid, Unsupported, Failed };
  struct Capabilities {
    bool size, pagerSizes, messageLed, colorful, appearance, glanceLocked, sensors, rotation;
  };
  struct Host {
    void *context;
    uint16_t (*readTimeout)(void *);
    bool (*writeTimeout)(void *, uint16_t);
    void (*flagChanged)(void *, Flag, bool);
    void (*restart)(void *, Restart);
    // The message LED preference exists only on Tanmatsu builds.
    bool (*readMessageLed)(void *);
    void (*writeMessageLed)(void *, bool);
    Capabilities capabilities;
  };
  struct State {
    bool flags[FlagCount]{};
    uint16_t timeout = 0;
    uint8_t size = 0, theme = 0, rotation = 0;
  };
  explicit DisplaySettings(Host host) : _host(host) {}
  Capabilities capabilities() const { return _host.capabilities; }
  State read() const;
  bool supports(Flag) const;
  bool setFlag(Flag, bool);
  static bool parseTimeout(const char *, uint16_t &);
  Result setTimeout(const char *, uint16_t &seconds);
  Result setSize(unsigned);
  Result setTheme(unsigned);
  Result rotate();

private:
  Host _host;
};
} // namespace ui
