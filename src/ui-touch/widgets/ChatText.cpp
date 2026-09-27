// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatText.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include <cstdio>
#include <cstring>
namespace ui {
namespace widgets {
using namespace ui::theme;
static constexpr uint32_t k_ts_epoch_min = 1700000000u;
void fmtClockHM(char *buf, size_t cap, const struct tm *t) {
  if (!buf || cap < 1)
    return;
#if defined(ESP32)
  if (touchPrefsGetClock12h()) {
    int h = t->tm_hour % 12;
    if (h == 0)
      h = 12;
    snprintf(buf, cap, "%d:%02d %s", h, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
    return;
  }
#endif
  strftime(buf, cap, "%H:%M", t);
}

void formatBubbleHhMm(uint32_t ts, char *out, int cap) {
  if (cap <= 0 || !out)
    return;
  if (ts < k_ts_epoch_min) {
    out[0] = '\0';
    return;
  }
  time_t t = (time_t)ts;
  struct tm tm_loc {};
  ui::platform::localTime(t, tm_loc);
  fmtClockHM(out, (size_t)cap, &tm_loc); // honor the 12/24-hour pref
}

void formatBubbleTs(uint32_t ts, char *out, int cap) {
  if (cap <= 0 || !out)
    return;
  out[0] = '\0';
  if (ts < k_ts_epoch_min)
    return;
  time_t t = (time_t)ts;
  time_t now = time(nullptr);
  struct tm tmv {
  }, tmn{};
  ui::platform::localTime(t, tmv);
  ui::platform::localTime(now, tmn);
  char time_part[12];
  fmtClockHM(time_part, sizeof(time_part), &tmv);
  if (tmv.tm_year == tmn.tm_year && tmv.tm_yday == tmn.tm_yday) {
    snprintf(out, (size_t)cap, "%s", time_part);
  } else if (tmv.tm_year == tmn.tm_year) {
    char date_part[12];
    strftime(date_part, sizeof(date_part), "%d %b", &tmv);
    snprintf(out, (size_t)cap, "%s %s", date_part, time_part);
  } else {
    char date_part[12];
    strftime(date_part, sizeof(date_part), "%d/%m/%y", &tmv);
    snprintf(out, (size_t)cap, "%s %s", date_part, time_part);
  }
}

void formatFullTimestamp(uint32_t ts, char *out, int cap) {
  if (cap <= 0 || !out)
    return;
  if (ts < k_ts_epoch_min) {
    snprintf(out, cap, TR("(RTC unset)"));
    return;
  }
  time_t t = (time_t)ts;
  struct tm tm_loc {};
  ui::platform::localTime(t, tm_loc);
  snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d", tm_loc.tm_year + 1900, tm_loc.tm_mon + 1,
           tm_loc.tm_mday, tm_loc.tm_hour, tm_loc.tm_min, tm_loc.tm_sec);
}

void usernameBubbleColors(const char *name, lv_color_t *bubble_bg, lv_color_t *name_col) {
  uint32_t h = 2166136261u; // FNV-1a offset basis
  for (const char *p = name; p && *p; ++p) {
    h ^= (uint8_t)(*p);
    h *= 16777619u;
  }
  const uint16_t hue = (uint16_t)(h % 360u);
  if (bubble_bg)
    *bubble_bg = lv_color_hsv_to_rgb(hue, 55, 26); // dark, off-white text readable
  if (name_col)
    *name_col = lv_color_hsv_to_rgb(hue, 85, 95); // vivid sender-name line
}

void recolorEscape(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  for (const char *s = src; *s && o + 2 < cap; ++s) {
    dst[o++] = *s;
    if (*s == '#')
      dst[o++] = '#';
  }
  dst[o] = '\0';
}

void formatDaySeparator(char *buf, size_t cap, const struct tm *tv) {
  time_t nowt = time(nullptr);
  struct tm nowv;
  ui::platform::localTime(nowt, nowv);
  time_t yt = nowt - 86400;
  struct tm yv;
  ui::platform::localTime(yt, yv);
  if (tv->tm_year == nowv.tm_year && tv->tm_yday == nowv.tm_yday)
    snprintf(buf, cap, "%s", TR("Today"));
  else if (tv->tm_year == yv.tm_year && tv->tm_yday == yv.tm_yday)
    snprintf(buf, cap, "%s", TR("Yesterday"));
  else if (tv->tm_year == nowv.tm_year)
    strftime(buf, cap, "%a %d %b", tv);
  else
    strftime(buf, cap, "%d %b %Y", tv);
}

lv_coord_t chatMeasureDaySepHeight() { return lv_font_get_line_height(&font12()) + 6; }

bool chatMsgDayKey(const ui::MessageTypes::UIMessage &m, long *out_key) {
  if (!out_key || m.ts < 1577836800UL)
    return false;
  time_t tt = (time_t)m.ts;
  struct tm tv;
  ui::platform::localTime(tt, tv);
  *out_key = (long)tv.tm_year * 512L + tv.tm_yday;
  return true;
}
} // namespace widgets
} // namespace ui
