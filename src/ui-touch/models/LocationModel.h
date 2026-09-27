// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
double distanceKm(double lat1, double lon1, double lat2, double lon2);
class LocationModel {
public:
  bool hadFix() const { return _had_fix; }
  void setBaseline(double lat, double lon) { _saved_lat = lat; _saved_lon = lon; }
  // A valid fix updates the live location in the caller. This policy only
  // decides when to persist it (movement > ~11 m, at most once per 2 minutes).
  bool shouldPersist(double lat, double lon, uint32_t now);
private:
  bool _had_fix = false, _persisted = false;
  double _saved_lat = 0, _saved_lon = 0;
  uint32_t _last_persist = 0;
};
}
