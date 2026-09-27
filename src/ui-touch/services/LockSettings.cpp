// SPDX-License-Identifier: GPL-3.0-or-later
#include "LockSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../models/ColorChoice.h"
#include <cstdio>
#include <cstring>
namespace ui {
LockSettings::State LockSettings::read() const {
  State state{};
  state.autoLock = touchPrefsGetLockOnScreenOff();
  state.color = touchPrefsGetLockTextColor();
  static_assert(sizeof state.wallpaper >= TOUCH_LOCK_WALLPAPER_MAXLEN,
                "Wallpaper snapshot must not truncate identity");
  touchPrefsGetLockWallpaper(state.wallpaper, sizeof state.wallpaper);
  return state;
}
bool LockSettings::setLocked(bool on) {
  if (!_host.capabilities.autoLock)
    return false;
  touchPrefsSetLockOnScreenOff(on);
  if (_host.lockChanged)
    _host.lockChanged(_host.context, on);
  return true; // Existing setter is void and cannot report persistence failures.
}
bool LockSettings::setColor(unsigned index) {
  return _host.capabilities.color && index < colorChoice::LockCount &&
         touchPrefsSetLockTextColor(colorChoice::lock(index));
}
void LockSettings::wallpaperName(const char *path, char *out, size_t capacity) {
  if (!out || !capacity)
    return;
  if (!path || !*path) {
    snprintf(out, capacity, "(default)");
    return;
  }
  const bool sd = !strncmp(path, "sd:", 3);
  const char *p = sd ? path + 3 : path;
  const char *name = strrchr(p, '/');
  snprintf(out, capacity, "%s%s", sd ? "SD: " : "", name ? name + 1 : p);
}
} // namespace ui
