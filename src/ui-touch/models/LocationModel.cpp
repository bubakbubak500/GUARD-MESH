#include "LocationModel.h"
#include <cmath>
namespace ui {
double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  constexpr double radians = 3.14159265358979323846 / 180.0;
  const double dlat = (lat2 - lat1) * radians;
  const double dlon = (lon2 - lon1) * radians;
  double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
             std::cos(lat1 * radians) * std::cos(lat2 * radians) *
             std::sin(dlon / 2) * std::sin(dlon / 2);
  // Rounding near antipodes must not feed a negative value to sqrt.
  if (a < 0) a = 0;
  if (a > 1) a = 1;
  return 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}
bool LocationModel::shouldPersist(double lat, double lon, uint32_t now) {
  if (!std::isfinite(lat) || !std::isfinite(lon) || (lat == 0 && lon == 0)) return false;
  _had_fix = true;
  if (_persisted && uint32_t(now - _last_persist) < 120000u) return false;
  if (std::fabs(lat - _saved_lat) <= 1e-4 && std::fabs(lon - _saved_lon) <= 1e-4) return false;
  setBaseline(lat, lon);
  _last_persist = now;
  _persisted = true;
  return true;
}
}
