// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
namespace notification {
constexpr unsigned ColorCount = 7;
bool quietWindow(bool enabled, int minute, unsigned startSlot, unsigned endSlot);
uint8_t stepSlot(unsigned slot, int direction);
void formatSlot(unsigned slot, char *out, size_t capacity);
uint32_t colorRgb(unsigned index);
// Abstract RGB bits, mapped by the device adapter to keyboard outputs.
uint8_t colorMask(unsigned index);
int soundSlot(bool direct, bool mention, bool messages, bool directs, bool mentions, bool messageMuted,
              bool mentionMuted);
} // namespace notification
} // namespace ui
