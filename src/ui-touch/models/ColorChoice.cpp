// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorChoice.h"
namespace ui {
namespace colorChoice {
uint32_t accent(unsigned index) {
  static const uint32_t values[] = {0x15B6A6, 0x57585A, 0x3B82F6, 0x2DA8A0, 0x3FA34D, 0x8B5CF6,
                                    0xD2569E, 0xD0524A, 0xE0823C, 0xC8A030, 0x5B6BD0, 0xA85AB0};
  static_assert(sizeof values / sizeof values[0] == AccentCount, "Accent palette size");
  return index < AccentCount ? values[index] : DefaultAccent;
}
uint32_t lock(unsigned index) {
  static const uint32_t values[] = {0xE6F2FF, 0xFFFFFF, 0x5BC0FF, 0x5BE5A0,
                                    0xFFC857, 0xFF6B6B, 0xB39DFF, 0x9AA7B4};
  static_assert(sizeof values / sizeof values[0] == LockCount, "Lock palette size");
  return index < LockCount ? values[index] : values[0];
}
bool parseHex(const char *text, uint32_t &output) {
  if (!text)
    return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < 6; ++i) {
    const char c = text[i];
    unsigned digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      digit = c - 'A' + 10;
    else
      return false;
    value = (value << 4) | digit;
  }
  if (text[6])
    return false;
  output = value;
  return true;
}
} // namespace colorChoice
} // namespace ui
