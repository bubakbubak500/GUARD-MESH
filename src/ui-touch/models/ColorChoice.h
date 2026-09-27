// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
namespace colorChoice {
constexpr uint32_t DefaultAccent = 0x15B6A6;
constexpr unsigned AccentCount = 12, LockCount = 8;
uint32_t accent(unsigned index);
uint32_t lock(unsigned index);
bool parseHex(const char *, uint32_t &);
} // namespace colorChoice
} // namespace ui
