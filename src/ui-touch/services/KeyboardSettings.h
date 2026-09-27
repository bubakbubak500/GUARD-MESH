// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../KeyboardLayouts.h"
#include "../models/KeyBindings.h"
namespace ui {
// UI-thread preferences and cached input bindings. Pending backlight saves belong
// to this service, so closing a form does not discard the user's last adjustment.
class KeyboardSettings {
public:
  enum Flag {
    Accent,
    EnterSends,
    Flash,
    Legacy,
    KeyboardNav,
    TrackballNav,
    MenuLetters,
    Reverse,
    Edge,
    FlagCount
  };
  enum class Light { None, Mode, Brightness };
  enum class Navigation { None, All, ExtraDirections };
  struct Capabilities {
    Light light;
    Navigation navigation;
    bool physicalCycle, enterFlash, legacy, trackball;
  };
  struct Host {
    void *context;
    void (*flagChanged)(void *, Flag, bool);
    void (*layoutsChanged)(void *);
    void (*keysChanged)(void *);
    void (*lightChanged)(void *, uint8_t mode, uint8_t percent);
    Capabilities capabilities;
  };
  struct State {
    bool flags[FlagCount]{};
    uint16_t layouts = 0;
    uint8_t lightMode = 2, brightness = 100;
    uint8_t keys[KeyBindings::Count]{};
  };
  explicit KeyboardSettings(Host host)
      : _host(host), _bindings(host.capabilities.navigation == Navigation::ExtraDirections) {}
  KeyboardSettings(const KeyboardSettings &) = delete;
  KeyboardSettings &operator=(const KeyboardSettings &) = delete;
  void load();
  State read();
  Capabilities capabilities() const { return _host.capabilities; }
  bool supports(Flag) const;
  bool setFlag(Flag, bool);
  bool setLayout(unsigned, bool);
  static const char *layoutName(unsigned);
  bool supportsBinding(unsigned) const;
  KeyBindings::Result assign(unsigned, int key);
  uint8_t key(unsigned index) {
    load();
    return _bindings.key(index);
  }
  int tabFor(int key) {
    load();
    return _bindings.tab(key);
  }
  int directionFor(int key) {
    load();
    return _bindings.direction(key);
  }
  uint8_t lightMode() {
    load();
    return _lightMode;
  }
  uint8_t brightness() {
    load();
    return _brightness;
  }
  uint8_t lightLevel() { return KeyBindings::backlightLevel(brightness()); }
  void setBrightness(int percent, uint32_t now);
  void lightOff(uint32_t now);
  void cycleLight(uint32_t now);
  bool saveLight();
  void tick(uint32_t now);
  bool lightSavePending() const { return _lightPending; }
  bool lastLightSaveOk() const { return _lightSaved; }

private:
  void lightChanged(uint32_t now);
  Host _host;
  KeyBindings _bindings;
  uint8_t _lightMode = 2, _brightness = 100;
  bool _loaded = false, _lightPending = false, _lightSaved = true;
  uint32_t _lightSaveAt = 0;
};
} // namespace ui
