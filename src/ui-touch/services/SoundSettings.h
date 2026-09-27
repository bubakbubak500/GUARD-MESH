// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/NotificationPolicy.h"
namespace ui {
class SoundSettings {
public:
  static constexpr unsigned SlotCount = 3;
  enum Flag { Master, Loud, Messages, Direct, Mentions, Dnd, Indicator, FlagCount };
  enum class Preview { Played, Silent, Quiet, DoNotDisturb };
  struct Capabilities {
    bool sound, loud, volume, files, indicator;
  };
  struct Host {
    void *context;
    bool (*quiet)(void *);
    void (*setQuiet)(void *, bool);
    int (*localMinute)(void *); // -1 when the wall clock is not trustworthy
    void (*play)(void *, unsigned slot);
    void (*volume)(void *, uint8_t);
    bool (*indicator)(void *, uint8_t rgbMask);
    void (*changed)(void *);
    Capabilities capabilities;
  };
  struct State {
    bool flags[FlagCount]{};
    uint8_t start = 0, end = 0, volume = 0, colors[2]{};
  };
  explicit SoundSettings(Host host) : _host(host) {}
  SoundSettings(const SoundSettings &) = delete;
  SoundSettings &operator=(const SoundSettings &) = delete;
  Capabilities capabilities() const { return _host.capabilities; }
  State read() const;
  bool supports(Flag) const;
  bool setFlag(Flag, bool on, uint32_t now);
  void stepTime(bool end, int direction);
  void stepVolume(int direction);
  bool setColor(unsigned row, unsigned color);
  bool dndActive() const;
  Preview preview(unsigned slot);
  Preview notifyMessage(bool direct, bool mention, bool messageMuted, bool mentionMuted);
  void fileName(unsigned slot, char *out, size_t capacity) const;
  bool useBuiltin(unsigned slot);
  bool selectFileSlot(unsigned slot);
  unsigned fileSlot() const { return _fileSlot; }
  void blink(bool direct, uint32_t now);
  void tick(uint32_t now);

private:
  void changed();
  void stopBlink(uint32_t now);
  Host _host;
  unsigned _fileSlot = 0;
  uint32_t _blinkAt = 0, _blinkGeneration = 0;
  uint8_t _blinkMask = 0, _transitions = 0;
  bool _lit = false, _writing = false;
};
} // namespace ui
