// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class LocationProvider {
public:
  bool isEnabled() { return false; }
  bool isValid() { return false; }
  int satellitesCount() { return 0; }
  long getLatitude() { return 0; }
  long getLongitude() { return 0; }
  long getAltitude() { return 0; }
  void syncTime() {}
  long getTimestamp() { return 0; }
};
class SensorManager {
public:
  double node_lat = 0, node_lon = 0, node_altitude = 0;
  const char *getSettingByKey(const char *) { return "0"; }
  bool setSettingValue(const char *, const char *) { return false; }
  LocationProvider *getLocationProvider() { return nullptr; }
};
