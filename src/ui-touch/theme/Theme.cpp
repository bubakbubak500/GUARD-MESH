#include "Theme.h"
#include "../device_caps.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui { namespace theme {
// ---- colour palette: TACTICAL (sober) ----
// Aimed at how *actual* fielded military comms equipment looks — Harris
// PRC handhelds, Codan terminals, Nett Warrior / MFOCS dashboards —
// rather than the Hollywood "amber + OD-green camo" look. Real military
// UIs are deliberately boring: dark neutral gray, clean white text, and
// colour reserved strictly for FUNCTIONAL status (amber = caution, green
// = OK, red = danger). The base palette here is all desaturated grays
// and one cool slate accent; the warm/saturated colours (the status
// amber + OD green + alert red) live in the status-color constants
// below and are used only where state really matters.
struct TouchPalette {
  uint32_t bg;
  uint32_t panel;
  uint32_t text;
  uint32_t sub;
  uint32_t sent_bg;
  uint32_t recv_bg;
  uint32_t mention;
  uint32_t mention_bg;
  uint32_t status_ok;
  uint32_t status_ok_pressed;
  uint32_t status_warn;
  uint32_t status_danger;
  uint32_t status_danger_pressed;
  uint32_t status_ok_text;
  uint32_t status_warn_text;
  uint32_t status_danger_text;
  uint32_t status_info;
  uint32_t border;
  uint32_t field;
  uint32_t control;
  uint32_t control_disabled;
  uint32_t control_pressed;
  uint32_t accent_surface;
  uint32_t accent_border;
  uint32_t chart_grid;
  uint32_t chart_tick;
  uint32_t raised;
  uint32_t secondary_action;
  uint32_t track;
  uint32_t chart_bg;
};

static constexpr TouchPalette kNightPalette = {
  0x000000, 0x040506, 0xE0E3E6, 0x828891,
  0x1D2226, 0x1B1D1F, 0x4FA3FF, 0x16324F,
  0x4A8E4A, 0x3B7039, 0xC8A030, 0xA04040, 0x7A2A2A,
  0x4A8E4A, 0xC8A030, 0xA04040,
  0x4F9DF7,
  0x18191A, 0x0A0B0C, 0x1A1B1C, 0x0C0D0E, 0x141516,
  0x1B2B3A, 0x2A3D52, 0x1A1D1F, 0x2A2E30,
  0x121417, 0x3A4A5C, 0x202428, 0x0B0D0F,
};

static constexpr TouchPalette kDayPalette = {
  0xF1F4F6, 0xFFFFFF, 0x172026, 0x52606C,
  0xDCE8EE, 0xE7EAED, 0x1F66A5, 0xDCEEFF,
  0x8FC395, 0x70A978, 0xE1C15A, 0xD99191, 0xC47777,
  0x276738, 0x806000, 0xA52B26,
  0xA8CDF0,
  0xC7D0D7, 0xF8FAFB, 0xE5EAEE, 0xD5DADF, 0xD4DCE2,
  0xD9EEEC, 0x6F9E99, 0xD6DDE2, 0xAAB5BD,
  0xFFFFFF, 0xD8E0E6, 0xD5DDE3, 0xF8FAFB,
};

static bool s_theme_day = false;
static Colors state = {
  kNightPalette.bg,
  kNightPalette.panel,
  0x15B6A6,
  0x0D766B,
  kNightPalette.text,
  kNightPalette.text,
  kNightPalette.text,
  kNightPalette.text,
  kNightPalette.text,
  kNightPalette.sub,
  0x4EA1FF,
  kNightPalette.sent_bg,
  kNightPalette.recv_bg,
  kNightPalette.mention_bg,
  kNightPalette.text,
  kNightPalette.sub,
  kNightPalette.sent_bg,
  kNightPalette.recv_bg,
  kNightPalette.mention,
  kNightPalette.mention_bg,
  kNightPalette.status_ok,
  kNightPalette.status_ok_pressed,
  kNightPalette.status_warn,
  kNightPalette.status_danger,
  kNightPalette.status_danger_pressed,
  kNightPalette.status_ok_text,
  kNightPalette.status_warn_text,
  kNightPalette.status_danger_text,
  kNightPalette.status_info,
  kNightPalette.border,
  kNightPalette.field,
  kNightPalette.control,
  kNightPalette.control_disabled,
  kNightPalette.control_pressed,
  kNightPalette.accent_surface,
  kNightPalette.accent_border,
  kNightPalette.chart_grid,
  kNightPalette.chart_tick,
  kNightPalette.raised,
  kNightPalette.secondary_action,
  kNightPalette.track,
  kNightPalette.chart_bg,
};
const Colors& colors() { return state; }
bool isDay() { return s_theme_day; }
// Earlier accent was 0x4E5C66 — RGB (78,92,102), cool blue-leaning. Even
// at low opacity that tinted every chip and settings row blue. Switched
// to a true neutral medium gray (very slight warm) so chip fills read as
// "darker gray" rather than "blue".
// Runtime-themeable accent (Settings -> Accent colour). NOT constexpr: the picker
// rewrites these live and they're reloaded from the saved pref at boot. Every
// accent site reads them through lv_color_hex(), so one write re-themes the UI.

// Perceived luminance 0..255 (keep the accent dark enough for off-white text).
uint32_t accentLuma(uint32_t rgb) {
  return (299u*((rgb>>16)&0xFF) + 587u*((rgb>>8)&0xFF) + 114u*(rgb&0xFF)) / 1000u;
}
// Scale brightness to `pct` percent, preserving hue.
uint32_t accentDarken(uint32_t rgb, int pct) {
  uint32_t r=((rgb>>16)&0xFF)*pct/100, g=((rgb>>8)&0xFF)*pct/100, b=(rgb&0xFF)*pct/100;
  return (r<<16)|(g<<8)|b;
}
// Clamp a picked accent dark enough that text/icons stay readable on solid fills.
uint32_t accentClampReadable(uint32_t rgb) {
  const uint32_t kMaxLuma = s_theme_day ? 105 : 140;
  uint32_t L = accentLuma(rgb);
  if (L > kMaxLuma) return accentDarken(rgb, (int)(kMaxLuma * 100 / L));
  return rgb & 0xFFFFFFu;
}
// Chat-bubble palette: kept near-monochrome (military comms terminals
// don't colour-code direction). Slight luminance + hue lean keeps L/R
// readable but neither side gets a "fun" tint.
// Functional status colours — use sparingly. These are the ONLY warm/
// saturated colours in the palette, so when they appear the operator
// instantly reads them as state, not decoration.

void applyThemeMode(uint8_t mode) {
#if defined(HAS_TDECK_PRO)
  mode = TOUCH_THEME_DAY;   // monochrome e-paper: black content on a white field
#endif
  s_theme_day = mode == TOUCH_THEME_DAY;
  const TouchPalette& p = s_theme_day ? kDayPalette : kNightPalette;
  state.COLOR_BG = p.bg;
  state.COLOR_PANEL = p.panel;
  state.COLOR_TEXT = p.text;
  state.COLOR_SUB = p.sub;
  state.COLOR_SENT_BG = p.sent_bg;
  state.COLOR_RECV_BG = p.recv_bg;
  state.COLOR_MENTION = p.mention;
  state.COLOR_MENTION_BG = p.mention_bg;
  state.COLOR_STATUS_OK = p.status_ok;
  state.COLOR_STATUS_OK_PRESSED = p.status_ok_pressed;
  state.COLOR_STATUS_WARN = p.status_warn;
  state.COLOR_STATUS_DANGER = p.status_danger;
  state.COLOR_STATUS_DANGER_PRESSED = p.status_danger_pressed;
  state.COLOR_STATUS_OK_TEXT = p.status_ok_text;
  state.COLOR_STATUS_WARN_TEXT = p.status_warn_text;
  state.COLOR_STATUS_DANGER_TEXT = p.status_danger_text;
  state.COLOR_STATUS_INFO = p.status_info;
  state.COLOR_BORDER = p.border;
  state.COLOR_FIELD = p.field;
  state.COLOR_CONTROL = p.control;
  state.COLOR_CONTROL_DISABLED = p.control_disabled;
  state.COLOR_CONTROL_PRESSED = p.control_pressed;
  state.COLOR_ACCENT_SURFACE = p.accent_surface;
  state.COLOR_ACCENT_BORDER = p.accent_border;
  state.COLOR_CHART_GRID = p.chart_grid;
  state.COLOR_CHART_TICK = p.chart_tick;
  state.COLOR_RAISED = p.raised;
  state.COLOR_SECONDARY_ACTION = p.secondary_action;
  state.COLOR_TRACK = p.track;
  state.COLOR_CHART_BG = p.chart_bg;
  state.COLOR_ON_ACCENT = s_theme_day ? 0xFFFFFFu : p.text;
  state.COLOR_ON_STATUS_OK = s_theme_day ? 0x15351Du : p.text;
  state.COLOR_ON_STATUS_DANGER = s_theme_day ? 0x571515u : p.text;
  state.COLOR_ON_STATUS_INFO = s_theme_day ? 0x103A5Au : p.text;
  state.COLOR_CHAT_TEXT = s_theme_day ? 0xFFFFFFu : p.text;
  state.COLOR_CHAT_META = s_theme_day ? 0xFFFFFFu : p.sub;
  state.COLOR_CHAT_LINK = s_theme_day ? 0x9EDBFFu : 0x4EA1FFu;
  state.COLOR_CHAT_SENT_BG = s_theme_day ? 0x28556Bu : p.sent_bg;
  state.COLOR_CHAT_RECV_BG = s_theme_day ? 0x3C4852u : p.recv_bg;
  state.COLOR_CHAT_MENTION_BG = s_theme_day ? 0x1D5F8Au : p.mention_bg;
}

uint32_t themeRole(uint32_t night, uint32_t day) {
  return s_theme_day ? day : night;
}

void setAccent(uint32_t rgb) {
  state.COLOR_ACCENT = accentClampReadable(rgb);
  state.COLOR_ACCENT_PRESS = accentDarken(state.COLOR_ACCENT, 65);
}
} }
