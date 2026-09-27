// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotificationPolicy.h"
#include <cstdio>
namespace ui {
namespace notification {
bool quietWindow(bool enabled, int minute, unsigned startSlot, unsigned endSlot) {
  if (!enabled || minute < 0 || minute >= 1440 || startSlot >= 48 || endSlot >= 48)
    return false;
  const int start = startSlot * 30, end = endSlot * 30;
  return start <= end ? minute >= start && minute < end : minute >= start || minute < end;
}
uint8_t stepSlot(unsigned slot, int direction) {
  slot %= 48;
  return (slot + (direction < 0 ? 47 : direction > 0 ? 1 : 0)) % 48;
}
void formatSlot(unsigned slot, char *out, size_t capacity) {
  if (!out || !capacity)
    return;
  slot %= 48;
  snprintf(out, capacity, "%02u:%02u", slot / 2, slot % 2 * 30);
}
uint32_t colorRgb(unsigned index) {
  static constexpr uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0x00ffff, 0xff00ff, 0xffffff};
  return colors[index < ColorCount ? index : 0];
}
uint8_t colorMask(unsigned index) {
  const auto rgb = colorRgb(index);
  return ((rgb & 0xff0000) ? 4 : 0) | ((rgb & 0x00ff00) ? 2 : 0) | ((rgb & 0x0000ff) ? 1 : 0);
}
int soundSlot(bool direct, bool mention, bool messages, bool directs, bool mentions, bool messageMuted,
              bool mentionMuted) {
  if (mention && mentions && !mentionMuted)
    return 2;
  if (direct)
    return directs ? 1 : -1;
  return messages && !messageMuted ? 0 : -1;
}
} // namespace notification
} // namespace ui
