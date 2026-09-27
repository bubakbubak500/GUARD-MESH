#include "BatteryModel.h"
namespace ui {
bool BatteryModel::isCharging(uint16_t mv) const { return mv && uint32_t(mv) >= uint32_t(fullMv()) + 50u; }
int BatteryModel::percent(uint16_t mv) const {
  if (!mv) return -1;
  if (mv >= fullMv()) return 100;
  if (mv <= 3300) return 0;
  return int((uint32_t(mv) - 3300u) * 100u / (fullMv() - 3300u));
}
uint16_t BatteryModel::sample(uint16_t raw, bool smooth) {
  if (!raw || !smooth) return raw;
  const float delta = float(raw) - _ema;
  if (_ema < 1 || delta > 70 || delta < -70) _ema = float(raw);
  else _ema += delta * 0.15f;
  return uint16_t(_ema + 0.5f);
}
uint16_t BatteryModel::publish(uint16_t sampled, uint32_t now) {
  if (!sampled) return 0;
  const bool flipped = _published && isCharging(sampled) != isCharging(_published);
  if (!_published || flipped || uint32_t(now - _published_at) >= 20000u) {
    _published = sampled;
    _published_at = now;
  }
  return _published;
}
}
