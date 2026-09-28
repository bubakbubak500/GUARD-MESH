// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace ui {
struct PingStatus {
  uint32_t batteryMv = 0, uptimeSecs = 0, queueLength = 0;
  int16_t rssi = 0;
  bool hasBattery = false, hasUptime = false, hasQueue = false, hasRssi = false;
  static PingStatus parse(const uint8_t* data, size_t len);
  // Remote calibration/chemistry is not transmitted. Use the default 1S
  // estimate, never this device's calibrated full voltage. -1 means unknown.
  int batteryPercent() const;
};
}
