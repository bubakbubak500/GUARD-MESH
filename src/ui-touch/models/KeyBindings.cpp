// SPDX-License-Identifier: GPL-3.0-or-later
#include "KeyBindings.h"
#include <cstring>
namespace ui {
KeyBindings::KeyBindings(bool extraDirections) {
  memcpy(_keys, "ertui", Tabs);
  const uint8_t standard[] = {'w', 'z', 'a', 'd', 's', 'q', 'f', 'c'};
  const uint8_t extra[] = {'w', 'x', 'a', 'd', 's', 0, 'f', 'v'};
  memcpy(_keys + Tabs, extraDirections ? extra : standard, Directions);
}
int KeyBindings::lower(int key) { return key >= 'A' && key <= 'Z' ? key - 'A' + 'a' : key; }
const char *KeyBindings::name(unsigned index) {
  static const char *names[] = {"Messages", "Contacts", "Home",   "Map",  "Settings",  "Up",         "Down",
                                "Left",     "Right",    "Select", "Back", "Scroll up", "Scroll down"};
  return index < Count ? names[index] : "";
}
uint8_t KeyBindings::backlightLevel(unsigned percent) {
  if (!percent)
    return 0;
  if (percent > 100)
    percent = 100;
  const auto level = (percent * percent * 255u + 5000u) / 10000u;
  return level ? level : 1;
}
void KeyBindings::restore(unsigned index, uint8_t key) {
  if (index < Count && key)
    _keys[index] = lower(key);
}
KeyBindings::Result KeyBindings::validate(unsigned index, int key) const {
  if (index >= Count)
    return Result::OutOfRange;
  if (key == 0x1b || key == 8 || key == 127)
    return Result::Cancelled;
  key = lower(key);
  if (key < 'a' || key > 'z')
    return Result::Invalid;
  for (unsigned i = 0; i < Count; ++i)
    if (i != index && _keys[i] && lower(_keys[i]) == key)
      return Result::Duplicate;
  return Result::Ok;
}
int KeyBindings::tab(int key) const {
  key = lower(key);
  for (unsigned i = 0; i < Tabs; ++i)
    if (_keys[i] && lower(_keys[i]) == key)
      return i;
  return -1;
}
int KeyBindings::direction(int key) const {
  key = lower(key);
  for (unsigned i = 0; i < Directions; ++i)
    if (_keys[Tabs + i] && lower(_keys[Tabs + i]) == key)
      return i;
  return -1;
}
} // namespace ui
