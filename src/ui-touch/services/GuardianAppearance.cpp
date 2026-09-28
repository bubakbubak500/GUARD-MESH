// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianAppearance.h"
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
#include <Preferences.h>
#else
namespace { uint8_t savedAppearance = 0; }
#endif
namespace guardian {
Appearance loadAppearance() {
  uint8_t value = 0;
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  Preferences prefs;
  if (prefs.begin("guardian-ui", true)) { value = prefs.getUChar("theme", 0); prefs.end(); }
#else
  value = savedAppearance;
#endif
  return value == 1 ? Appearance::Green : Appearance::Blue;
}
bool saveAppearance(Appearance value) {
  if (value != Appearance::Blue && value != Appearance::Green) return false;
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  Preferences prefs;
  if (!prefs.begin("guardian-ui", false)) return false;
  const bool ok = prefs.putUChar("theme", static_cast<uint8_t>(value)) == 1;
  prefs.end(); return ok;
#else
  savedAppearance = static_cast<uint8_t>(value); return true;
#endif
}
}
