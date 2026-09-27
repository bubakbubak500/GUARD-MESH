// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
namespace ui {
namespace sightline {
constexpr int Samples = 24;
constexpr double EarthRadius = 8504000.0; // effective 4/3 earth radius, metres
struct Path {
  double selfLat, selfLon, peerLat, peerLon;
};
enum Verdict { Clear, Marginal, Blocked };
struct Analysis {
  double distanceKm, bearing, frequency, h0, hN;
  double clearance, fresnelMargin, worstDistance, worstFresnel;
  int worstIndex;
  Verdict verdict;
};
bool validPath(const Path &);
bool sampleUrl(const Path &, const char *server, char *out, size_t capacity);
int parseElevations(const char *, float *out, int count);
int validElevations(const float *, int count);
bool repairElevations(float *, int count, int parsed);
bool analyze(const Path &, const float *, double selfHeight, double peerHeight, double frequency, Analysis &);
const char *compass(double bearing);
} // namespace sightline
} // namespace ui
