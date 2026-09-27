// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
namespace clock {
struct LocalTime {
  int year, month, day, hour, minute;
};
enum class ParseResult { Ok, Format, Invalid };
ParseResult parse(const char *, LocalTime &);
// Uses the process's configured local zone. Rejects mktime normalization (for
// example a nonexistent spring-forward time), overflow and the replay floor.
bool toEpoch(const LocalTime &, uint32_t minimumEpoch, uint32_t &epoch);
} // namespace clock
} // namespace ui
