// SPDX-License-Identifier: GPL-3.0-or-later
#include "DisplaySettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui {
DisplaySettings::State DisplaySettings::read() const {
  State state;
  state.flags[Miles] = touchPrefsGetUseMiles();
  state.flags[Colorful] = touchPrefsGetColorfulBubbles();
  state.flags[Compact] = touchPrefsGetCompactChat();
  state.flags[HideName] = touchPrefsGetHideNodeName();
  state.flags[Glance] = touchPrefsGetGlanceEnabled();
  state.flags[GlanceLocked] = touchPrefsGetGlanceWhenLocked();
  state.flags[MessageLed] = _host.readMessageLed && _host.readMessageLed(_host.context);
  state.flags[Sensors] = touchPrefsGetShowSensorsTab();
  state.size = touchPrefsGetUiScale();
  state.theme = touchPrefsGetThemeMode();
  state.rotation = touchPrefsGetUiRotation();
  if (_host.readTimeout)
    state.timeout = _host.readTimeout(_host.context);
  return state;
}
bool DisplaySettings::supports(Flag flag) const {
  switch (flag) {
  case Miles:
  case Compact:
  case HideName:
  case Glance:
    return true;
  case Colorful:
    return _host.capabilities.colorful;
  case GlanceLocked:
    return _host.capabilities.glanceLocked;
  case MessageLed:
    return _host.capabilities.messageLed;
  case Sensors:
    return _host.capabilities.sensors;
  default:
    return false;
  }
}
bool DisplaySettings::setFlag(Flag flag, bool on) {
  if (!supports(flag))
    return false;
  bool saved = true;
  switch (flag) {
  case Miles:
    saved = touchPrefsSetUseMiles(on);
    break;
  case Colorful:
    saved = touchPrefsSetColorfulBubbles(on);
    break;
  case Compact:
    saved = touchPrefsSetCompactChat(on);
    break;
  case HideName:
    saved = touchPrefsSetHideNodeName(on);
    break;
  case Glance:
    touchPrefsSetGlanceEnabled(on);
    break;
  case GlanceLocked:
    touchPrefsSetGlanceWhenLocked(on);
    break;
  case MessageLed:
    if (!_host.writeMessageLed)
      return false;
    _host.writeMessageLed(_host.context, on);
    break;
  case Sensors:
    saved = touchPrefsSetShowSensorsTab(on);
    break;
  default:
    return false;
  }
  // Existing preference setters update their RAM cache even when persistence
  // fails, so platform consumers must observe the same current value.
  if (_host.flagChanged)
    _host.flagChanged(_host.context, flag, on);
  return saved;
}
bool DisplaySettings::parseTimeout(const char *text, uint16_t &seconds) {
  if (!text || !*text)
    return false;
  bool negative = *text == '-';
  if (negative)
    ++text;
  if (!*text)
    return false;
  unsigned value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9')
      return false;
    value = value * 10 + unsigned(*text - '0');
    if (value > 3600)
      value = 3600; // Saturate before the next multiply.
  }
  seconds = negative ? 0 : value && value < 10 ? 10 : value;
  return true;
}
DisplaySettings::Result DisplaySettings::setTimeout(const char *text, uint16_t &seconds) {
  if (!parseTimeout(text, seconds))
    return Result::Invalid;
  return _host.writeTimeout && _host.writeTimeout(_host.context, seconds) ? Result::Applied : Result::Failed;
}
DisplaySettings::Result DisplaySettings::setSize(unsigned size) {
  if (!_host.capabilities.size)
    return Result::Unsupported;
  if (size >= (_host.capabilities.pagerSizes ? 4u : 3u))
    return Result::Invalid;
  return touchPrefsSetUiScale(size) ? Result::Applied : Result::Failed;
}
DisplaySettings::Result DisplaySettings::setTheme(unsigned theme) {
  if (!_host.capabilities.appearance)
    return Result::Unsupported;
  if (theme > TOUCH_THEME_DAY)
    return Result::Invalid;
  if (touchPrefsGetThemeMode() == theme)
    return Result::Unchanged;
  if (!touchPrefsSetThemeMode(theme))
    return Result::Failed;
  if (_host.restart)
    _host.restart(_host.context, theme == TOUCH_THEME_DAY ? Restart::Day : Restart::Night);
  return Result::Applied;
}
DisplaySettings::Result DisplaySettings::rotate() {
  if (!_host.capabilities.rotation)
    return Result::Unsupported;
  // Existing boot contract: 0 portrait, 1 landscape (LV_DISP_ROT_90).
  if (!touchPrefsSetUiRotation(touchPrefsGetUiRotation() ? 0 : 1))
    return Result::Failed;
  if (_host.restart)
    _host.restart(_host.context, Restart::Rotation);
  return Result::Applied;
}
} // namespace ui
