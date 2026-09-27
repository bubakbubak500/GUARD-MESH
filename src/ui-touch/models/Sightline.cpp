// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sightline.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ui {
namespace sightline {
bool validPath(const Path &p) {
  return std::isfinite(p.selfLat) && std::isfinite(p.peerLat) && std::isfinite(p.selfLon) &&
         std::isfinite(p.peerLon) && std::abs(p.selfLat) <= 90 && std::abs(p.peerLat) <= 90 &&
         std::abs(p.selfLon) <= 180 && std::abs(p.peerLon) <= 180;
}
bool sampleUrl(const Path &p, const char *server, char *out, size_t capacity) {
  if (!out || !capacity)
    return false;
  out[0] = 0;
  if (!server || !*server || !validPath(p))
    return false;
  size_t length = std::strlen(server);
  while (length && server[length - 1] == '/')
    --length;
  if (!length || length > 79)
    return false;
  int used = std::snprintf(out, capacity, "%.*s/elev?locations=", int(length), server);
  if (used < 0 || size_t(used) >= capacity) {
    out[0] = 0;
    return false;
  }
  // Preserve local linear sampling; cross the antimeridian by the short arc.
  double delta = p.peerLon - p.selfLon;
  if (delta > 180)
    delta -= 360;
  if (delta < -180)
    delta += 360;
  for (int i = 0; i < Samples; ++i) {
    double f = double(i) / (Samples - 1);
    double lon = p.selfLon + f * delta;
    if (lon > 180)
      lon -= 360;
    if (lon < -180)
      lon += 360;
    int n = std::snprintf(out + used, capacity - used, "%s%.5f,%.5f", i ? "|" : "",
                          p.selfLat + f * (p.peerLat - p.selfLat), lon);
    if (n < 0 || size_t(n) >= capacity - used) {
      out[0] = 0;
      return false;
    }
    used += n;
  }
  return true;
}
int parseElevations(const char *text, float *out, int count) {
  if (!text || !out || count <= 0)
    return 0;
  const char *p = text;
  int parsed = 0;
  while (*p && parsed < count) {
    while (std::isspace(static_cast<unsigned char>(*p)))
      ++p;
    if (!*p)
      break;
    float height = NAN;
    if (!std::strncmp(p, "null", 4))
      p += 4;
    else {
      char *end;
      double value = std::strtod(p, &end);
      if (end == p)
        return 0;
      height = static_cast<float>(value);
      if (!std::isfinite(height))
        height = NAN;
      p = end;
    }
    while (std::isspace(static_cast<unsigned char>(*p)))
      ++p;
    if (*p && *p != ',')
      return 0;
    out[parsed++] = height;
    if (*p == ',')
      ++p;
  }
  return parsed;
}
int validElevations(const float *values, int count) {
  int valid = 0;
  if (values)
    for (int i = 0; i < count; ++i)
      if (std::isfinite(values[i]))
        ++valid;
  return valid;
}
bool repairElevations(float *m, int n, int parsed) {
  if (!m || n < 2 || parsed < 0 || parsed > n)
    return false;
  for (int i = parsed; i < n; ++i)
    m[i] = NAN; // trailing missing points
  int valid = 0, first = -1, last = -1;
  for (int i = 0; i < n; ++i) {
    if (std::isfinite(m[i])) {
      ++valid;
      if (first < 0)
        first = i;
      last = i;
    }
  }
  // Need at least ~1/3 real coverage; below that the profile would be mostly
  // fabricated, so treat it as a fetch failure worth retrying.
  if (valid < 2 || valid * 3 < n)
    return false;
  for (int i = 0; i < first; ++i)
    m[i] = m[first]; // clamp leading gap
  for (int i = last + 1; i < n; ++i)
    m[i] = m[last]; // clamp trailing gap
  int i = first;
  while (i <= last) {
    if (std::isfinite(m[i])) {
      ++i;
      continue;
    }
    const int lo = i - 1; // last real sample before the gap
    int hi = i;
    while (hi <= last && !std::isfinite(m[hi]))
      ++hi; // first real sample after
    const float a = m[lo], b = m[hi];
    const int span = hi - lo;
    for (int k = lo + 1; k < hi; ++k)
      m[k] = static_cast<float>(double(a) + (double(b) - a) * (k - lo) / span);
    i = hi;
  }
  return true;
}

static double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * 3.14159265358979323846 / 180.0, p2 = lat2 * 3.14159265358979323846 / 180.0;
  const double dl = (lon2 - lon1) * 3.14159265358979323846 / 180.0;
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = atan2(y, x) * 180.0 / 3.14159265358979323846;
  if (b < 0)
    b += 360.0;
  return b;
}
const char *compass(double deg) {
  if (!std::isfinite(deg))
    return "?";
  deg = std::fmod(std::fmod(deg, 360.0) + 360.0, 360.0);
  static const char *d[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return d[(int)((deg + 22.5) / 45.0) & 7];
}

bool analyze(const Path &p, const float *elev, double selfHeight, double peerHeight, double frequency,
             Analysis &a) {
  if (!validPath(p) || validElevations(elev, Samples) != Samples || !std::isfinite(selfHeight) ||
      !std::isfinite(peerHeight))
    return false;
  selfHeight = std::max(0.0, std::min(150.0, selfHeight));
  peerHeight = std::max(0.0, std::min(150.0, peerHeight));
  a.frequency = std::isfinite(frequency) && frequency > 1.0 ? frequency : 868.0;
  const double rad = 3.14159265358979323846 / 180.0;
  double slat = std::sin((p.peerLat - p.selfLat) * rad / 2);
  double slon = std::sin((p.peerLon - p.selfLon) * rad / 2);
  double arc = slat * slat + std::cos(p.selfLat * rad) * std::cos(p.peerLat * rad) * slon * slon;
  a.distanceKm = 12742.0 * std::asin(std::sqrt(std::max(0.0, std::min(1.0, arc))));
  a.bearing = bearingDeg(p.selfLat, p.selfLon, p.peerLat, p.peerLon);
  a.h0 = elev[0] + selfHeight;
  a.hN = elev[Samples - 1] + peerHeight;
  a.clearance = a.fresnelMargin = 1e30;
  a.worstIndex = -1;
  a.worstDistance = a.worstFresnel = 0;
  const double distance = a.distanceKm * 1000.0;
  for (int i = 1; i < Samples - 1; ++i) {
    const double f = double(i) / (Samples - 1);
    const double d1 = f * distance, d2 = (1.0 - f) * distance;
    const double terrain = elev[i] + d1 * d2 / (2 * EarthRadius);
    const double clear = a.h0 + f * (a.hN - a.h0) - terrain;
    const double fresnel = distance > 0 ? std::sqrt((300.0 / a.frequency) * d1 * d2 / distance) : 0;
    if (clear < a.clearance) {
      a.clearance = clear;
      a.worstIndex = i;
      a.worstDistance = d1;
    }
    const double margin = clear - 0.6 * fresnel;
    if (margin < a.fresnelMargin) {
      a.fresnelMargin = margin;
      a.worstFresnel = fresnel;
    }
  }
  a.verdict = a.clearance < 0 ? Blocked : a.fresnelMargin < 0 ? Marginal : Clear;
  return true;
}
} // namespace sightline
} // namespace ui
