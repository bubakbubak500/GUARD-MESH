// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
// The production date/position parser is used. Only the unrelated calendar
// conversion is replaced; these tests count RTC writes, not epoch arithmetic.
class DateTime {
public:
  DateTime(int, int, int, int, int, int) {}
  uint32_t unixtime() const { return 1774269319; }
};
