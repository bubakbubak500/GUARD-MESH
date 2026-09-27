// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockTime.h"
#include <cstring>
#include <ctime>
namespace ui {
namespace clock {
static bool valid(const LocalTime &time) {
  if (time.year < 2000 || time.year > 2099 || time.month < 1 || time.month > 12 || time.day < 1 ||
      time.hour < 0 || time.hour > 23 || time.minute < 0 || time.minute > 59)
    return false;
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = time.year % 4 == 0 && (time.year % 100 != 0 || time.year % 400 == 0);
  return time.day <= days[time.month - 1] + (time.month == 2 && leap ? 1 : 0);
}
ParseResult parse(const char *text, LocalTime &out) {
  if (!text || strlen(text) != 16 || text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':')
    return ParseResult::Format;
  int digits[16]{};
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13)
      continue;
    if (text[i] < '0' || text[i] > '9')
      return ParseResult::Format;
    digits[i] = text[i] - '0';
  }
  LocalTime time{digits[0] * 1000 + digits[1] * 100 + digits[2] * 10 + digits[3], digits[5] * 10 + digits[6],
                 digits[8] * 10 + digits[9], digits[11] * 10 + digits[12], digits[14] * 10 + digits[15]};
  if (!valid(time))
    return ParseResult::Invalid;
  out = time;
  return ParseResult::Ok;
}
bool toEpoch(const LocalTime &local, uint32_t minimumEpoch, uint32_t &epoch) {
  if (!valid(local))
    return false;
  std::tm value{};
  value.tm_year = local.year - 1900;
  value.tm_mon = local.month - 1;
  value.tm_mday = local.day;
  value.tm_hour = local.hour;
  value.tm_min = local.minute;
  value.tm_isdst = -1;
  const auto converted = std::mktime(&value);
  if (converted < 0 || static_cast<uint64_t>(converted) > UINT32_MAX ||
      static_cast<uint64_t>(converted) <= minimumEpoch || value.tm_year != local.year - 1900 ||
      value.tm_mon != local.month - 1 || value.tm_mday != local.day || value.tm_hour != local.hour ||
      value.tm_min != local.minute || value.tm_sec != 0)
    return false;
  epoch = static_cast<uint32_t>(converted);
  return true;
}
} // namespace clock
} // namespace ui
