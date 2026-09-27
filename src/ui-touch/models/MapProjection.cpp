// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapProjection.h"
#include <cmath>
namespace ui { namespace maps {
namespace { constexpr double pi = 3.14159265358979323846; }
void latLonToWorldPx(double lat, double lon, uint8_t zoom, double* x, double* y) {
  const double scale = std::ldexp(256.0, zoom);
  *x = (lon + 180.0) / 360.0 * scale;
  const double sine = std::sin(lat * pi / 180.0);
  const double s = sine > 0.9999 ? 0.9999 : (sine < -0.9999 ? -0.9999 : sine);
  *y = (0.5 - std::log((1 + s) / (1 - s)) / (4 * pi)) * scale;
}
void worldPxToLatLon(double x, double y, uint8_t zoom, double* lat, double* lon) {
  const double scale = std::ldexp(256.0, zoom);
  *lon = x / scale * 360.0 - 180.0;
  *lat = std::atan(std::sinh(pi * (1 - 2 * y / scale))) * 180.0 / pi;
}
int gridRadius(int dimension) {
  return dimension <= 512 ? 1 : 2;
}
} }
