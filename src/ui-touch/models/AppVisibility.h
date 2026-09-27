// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui { namespace app_visibility {
// Built-in tile hide bits (app_hide pref). Order is FROZEN — append only.
enum {
  APPHIDE_SNAKE = 1u << 0,  APPHIDE_AIRTIME = 1u << 1, APPHIDE_MONITOR = 1u << 2,
  APPHIDE_SPECTRUM = 1u << 3, APPHIDE_DISCOVER = 1u << 4, APPHIDE_VNC = 1u << 5,
  APPHIDE_REMOTE = 1u << 6, APPHIDE_READER = 1u << 7, APPHIDE_TERMINAL = 1u << 8,
  APPHIDE_FILES = 1u << 9, APPHIDE_SIGNAL = 1u << 10, APPHIDE_MENTIONS = 1u << 11,
  APPHIDE_MQTT = 1u << 12,   // Settings -> MQTT bridge (hidden by default: experimental + privacy)
  APPHIDE_FILE_TRANSFER = 1u << 13,
  APPHIDE_USB_FILES = 1u << 14,
};
} }
