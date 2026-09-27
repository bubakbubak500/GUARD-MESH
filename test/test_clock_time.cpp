// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/ClockTime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <string>
void clockTimeRegression() {
  using namespace ui::clock;
  LocalTime local{};
  for (const auto *text : {"2024-02-29 23:59", "2000-02-29 00:00", "2099-12-31 12:00"})
    assert(parse(text, local) == ParseResult::Ok);
  for (const auto *text :
       {"2023-02-29 12:00", "2024-04-31 12:00", "2024-00-10 12:00", "2024-13-10 12:00", "2024-01-00 12:00",
        "2024-01-32 12:00", "2024-01-01 24:00", "2024-01-01 00:60", "1999-12-31 12:00", "2100-01-01 12:00"})
    assert(parse(text, local) == ParseResult::Invalid);
  for (const auto *text : {"", "2024-1-01 12:00", "2024-01-01 1:00", "2024-01-01 12:00x", "2024/01/01 12:00",
                           "2024-0x-01 12:00", "2024-01-01T12:00"})
    assert(parse(text, local) == ParseResult::Format);
  assert(parse(nullptr, local) == ParseResult::Format);
  const char *previous = std::getenv("TZ");
  const bool hadZone = previous != nullptr;
  const std::string zone = previous ? previous : "";
#ifdef _WIN32
  _putenv_s("TZ", "UTC0");
  _tzset();
#else
  setenv("TZ", "UTC0", 1);
  tzset();
#endif
  assert(parse("2024-01-01 00:00", local) == ParseResult::Ok);
  uint32_t epoch = 77;
  assert(toEpoch(local, 0, epoch) && epoch == 1704067200);
  assert(!toEpoch(local, 1704067200, epoch) && epoch == 1704067200);
  assert(!toEpoch(LocalTime{2024, 2, 31, 12, 0}, 0, epoch));
  assert(!toEpoch(LocalTime{2024, 3, 1, 30, 0}, 0, epoch));
#ifdef _WIN32
  (void)hadZone;
  _putenv_s("TZ", zone.c_str());
  _tzset();
#else
  if (hadZone)
    setenv("TZ", zone.c_str(), 1);
  else
    unsetenv("TZ");
  tzset();
#endif
}
