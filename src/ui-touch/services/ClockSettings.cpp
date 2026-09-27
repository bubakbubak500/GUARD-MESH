// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../platform/UiPlatform.h"
#include <cstdio>
namespace ui {
ClockSettings::State ClockSettings::read() const {
  return {touchPrefsGetTimezone(), touchPrefsGetTimeOffsetHours(), touchPrefsGetClock12h(),
          touchPrefsGetBootWifiTime(), touchPrefsGetBootWifiTimeOpen()};
}
int ClockSettings::zoneCount() const { return touchPrefsTimezoneCount(); }
const char *ClockSettings::zoneLabel(int index) const { return touchPrefsTimezoneLabel(index); }
void ClockSettings::applyTimezone() {
  char timezone[48];
  touchPrefsBuildLocalTz(timezone, sizeof timezone);
  platform::applyLocalTimezone(timezone);
}
void ClockSettings::setZone(int zone) {
  if (zone < 0 || zone >= zoneCount())
    return;
  touchPrefsSetTimezone(static_cast<uint8_t>(zone));
  applyTimezone();
}
int ClockSettings::stepOffset(int delta) {
  int offset = touchPrefsGetTimeOffsetHours();
  // UI commands are steps, not arbitrary integer additions.
  if (delta < 0 && offset > -23)
    --offset;
  else if (delta > 0 && offset < 23)
    ++offset;
  touchPrefsSetTimeOffsetHours(offset);
  applyTimezone();
  return offset;
}
void ClockSettings::setFlag(Flag flag, bool on) {
  switch (flag) {
  case Flag::Hour12:
    touchPrefsSetClock12h(on);
    break;
  case Flag::BootWifi:
    touchPrefsSetBootWifiTime(on);
    break;
  case Flag::OpenWifi:
    touchPrefsSetBootWifiTimeOpen(on);
    break;
  }
}
void ClockSettings::prefill(char *text, size_t capacity) const {
  if (!text || !capacity)
    return;
  text[0] = 0;
  if (!_host.now)
    return;
  tm value{};
  if (!platform::localTime(static_cast<time_t>(_host.now(_host.context)), value))
    return;
  if (value.tm_year < 100 || value.tm_year > 199)
    return;
  snprintf(text, capacity, "%04d-%02d-%02d %02d:%02d", value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
           value.tm_hour, value.tm_min);
}
clock::ParseResult ClockSettings::setManual(const char *text) {
  clock::LocalTime local{};
  const auto parsed = clock::parse(text, local);
  if (parsed != clock::ParseResult::Ok)
    return parsed;
  uint32_t epoch = 0;
  if (!_host.setTime || !clock::toEpoch(local, _host.minimumEpoch, epoch))
    return clock::ParseResult::Invalid;
  _host.setTime(_host.context, epoch);
  return clock::ParseResult::Ok;
}
} // namespace ui
