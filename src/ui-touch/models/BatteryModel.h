// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
class BatteryModel {
public:
  void setFullMv(uint16_t mv) { _full_mv = mv; _calibrated = true; }
  bool calibrationLoaded() const { return _calibrated; }
  uint16_t fullMv() const { return _full_mv ? _full_mv : 4200; }
  bool isCharging(uint16_t mv) const;
  int percent(uint16_t mv) const;
  uint16_t sample(uint16_t raw, bool smooth);
  uint16_t publish(uint16_t sampled, uint32_t now);
private:
  uint16_t _full_mv = 0, _published = 0;
  uint32_t _published_at = 0;
  bool _calibrated = false;
  float _ema = 0;
};
}
