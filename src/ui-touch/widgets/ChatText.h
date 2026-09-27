// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include <ctime>
#include <lvgl.h>
#include <stddef.h>
namespace ui {
namespace widgets {
void fmtClockHM(char *, size_t, const struct tm *);
void formatBubbleHhMm(uint32_t, char *, int);
void formatBubbleTs(uint32_t, char *, int);
void formatFullTimestamp(uint32_t, char *, int);
void usernameBubbleColors(const char *, lv_color_t *, lv_color_t *);
void recolorEscape(char *, size_t, const char *);
void formatDaySeparator(char *, size_t, const struct tm *);
lv_coord_t chatMeasureDaySepHeight();
bool chatMsgDayKey(const ui::MessageTypes::UIMessage &, long *);
} // namespace widgets
} // namespace ui
