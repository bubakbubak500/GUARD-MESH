// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace ui { namespace theme {
// Owned by the UI thread. Consumers receive a read-only palette; only the
// theme module mutates it, before a screen tree is built or restyled.
struct Colors {
  uint32_t COLOR_BG;
  uint32_t COLOR_PANEL;
  uint32_t COLOR_ACCENT;
  uint32_t COLOR_ACCENT_PRESS;
  uint32_t COLOR_ON_ACCENT;
  uint32_t COLOR_ON_STATUS_OK;
  uint32_t COLOR_ON_STATUS_DANGER;
  uint32_t COLOR_ON_STATUS_INFO;
  uint32_t COLOR_CHAT_TEXT;
  uint32_t COLOR_CHAT_META;
  uint32_t COLOR_CHAT_LINK;
  uint32_t COLOR_CHAT_SENT_BG;
  uint32_t COLOR_CHAT_RECV_BG;
  uint32_t COLOR_CHAT_MENTION_BG;
  uint32_t COLOR_TEXT;
  uint32_t COLOR_SUB;
  uint32_t COLOR_SENT_BG;
  uint32_t COLOR_RECV_BG;
  uint32_t COLOR_MENTION;
  uint32_t COLOR_MENTION_BG;
  uint32_t COLOR_STATUS_OK;
  uint32_t COLOR_STATUS_OK_PRESSED;
  uint32_t COLOR_STATUS_WARN;
  uint32_t COLOR_STATUS_DANGER;
  uint32_t COLOR_STATUS_DANGER_PRESSED;
  uint32_t COLOR_STATUS_OK_TEXT;
  uint32_t COLOR_STATUS_WARN_TEXT;
  uint32_t COLOR_STATUS_DANGER_TEXT;
  uint32_t COLOR_STATUS_INFO;
  uint32_t COLOR_BORDER;
  uint32_t COLOR_FIELD;
  uint32_t COLOR_CONTROL;
  uint32_t COLOR_CONTROL_DISABLED;
  uint32_t COLOR_CONTROL_PRESSED;
  uint32_t COLOR_ACCENT_SURFACE;
  uint32_t COLOR_ACCENT_BORDER;
  uint32_t COLOR_CHART_GRID;
  uint32_t COLOR_CHART_TICK;
  uint32_t COLOR_RAISED;
  uint32_t COLOR_SECONDARY_ACTION;
  uint32_t COLOR_TRACK;
  uint32_t COLOR_CHART_BG;
};
const Colors& colors();
bool isDay();
void applyThemeMode(uint8_t mode);
void setAccent(uint32_t rgb);
uint32_t accentLuma(uint32_t rgb);
uint32_t accentDarken(uint32_t rgb, int pct);
uint32_t accentClampReadable(uint32_t rgb);
uint32_t themeRole(uint32_t night, uint32_t day);
} }
