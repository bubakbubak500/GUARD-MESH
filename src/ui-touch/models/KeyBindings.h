// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
class KeyBindings {
public:
  static constexpr unsigned Tabs = 5, Directions = 8, Count = Tabs + Directions;
  enum class Result { Ok, Cancelled, Invalid, Duplicate, OutOfRange, SaveFailed };
  explicit KeyBindings(bool extraDirections = false);
  static int lower(int key);
  static const char *name(unsigned index);
  static uint8_t backlightLevel(unsigned percent);
  uint8_t key(unsigned index) const { return index < Count ? _keys[index] : 0; }
  void restore(unsigned index, uint8_t key);
  Result validate(unsigned index, int key) const;
  int tab(int key) const;
  int direction(int key) const;

private:
  uint8_t _keys[Count];
};
} // namespace ui
