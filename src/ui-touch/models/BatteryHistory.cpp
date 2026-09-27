// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryHistory.h"
#include <cmath>
#include <cstring>
namespace ui {
namespace battery {
namespace {
bool number(const char *&text, char delimiter, uint32_t maximum, uint32_t &value) {
  value = 0;
  if (*text < '0' || *text > '9')
    return false;
  do {
    const auto digit = static_cast<unsigned>(*text++ - '0');
    if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
      return false;
    value = value * 10 + digit;
  } while (*text >= '0' && *text <= '9');
  if (*text != delimiter)
    return false;
  if (delimiter)
    ++text;
  return true;
}
} // namespace
bool parseSample(const char *text, Sample &sample) {
  if (!text)
    return false;
  uint32_t epoch, voltage, percent, cpu = 0;
  if (!number(text, '\t', UINT32_MAX, epoch))
    return false;
  const char *dateEnd = strchr(text, '\t');
  if (!dateEnd || dateEnd == text || dateEnd - text > 19)
    return false;
  text = dateEnd + 1;
  if (!number(text, '\t', UINT16_MAX, voltage) || !voltage)
    return false;
  const bool withCpu = strchr(text, '\t') != nullptr;
  if (!number(text, withCpu ? '\t' : '\0', 100, percent))
    return false;
  if (withCpu && !number(text, '\0', UINT16_MAX, cpu))
    return false;
  sample = {epoch, static_cast<uint16_t>(voltage), static_cast<uint16_t>(cpu), static_cast<int>(percent)};
  return true;
}
Estimate estimate(const Sample *samples, size_t count, uint16_t fullMv) {
  if (!samples || !count)
    return {EstimateKind::Gathering, 0};
  size_t start = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto &sample = samples[i];
    if (sample.epoch < 1700000000u) {
      start = i + 1;
      continue;
    }
    if (i && (samples[i - 1].epoch < 1700000000u || sample.epoch <= samples[i - 1].epoch))
      start = i;
    if (sample.millivolts > fullMv)
      start = i + 1;
    else if (i && static_cast<int>(sample.millivolts) - samples[i - 1].millivolts >= 25)
      start = i;
  }
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  size_t used = 0;
  uint32_t origin = 0, lastTime = 0;
  int lastVoltage = 0;
  for (size_t i = start; i < count; ++i) {
    const auto &sample = samples[i];
    if (sample.millivolts > fullMv)
      continue;
    if (!used)
      origin = sample.epoch;
    const double x = sample.epoch - origin, y = sample.millivolts;
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    ++used;
    lastTime = sample.epoch;
    lastVoltage = sample.millivolts;
  }
  if (used < 3 || lastTime - origin < 900)
    return {EstimateKind::Gathering, 0};
  const double denominator = used * sxx - sx * sx;
  if (denominator <= 0)
    return {EstimateKind::Unavailable, 0};
  const double slope = (used * sxy - sx * sy) / denominator;
  if (slope >= -1e-7)
    return {EstimateKind::Steady, 0};
  const double seconds = (lastVoltage - 3300) / -slope;
  if (!std::isfinite(seconds))
    return {EstimateKind::Unavailable, 0};
  // With uint16 mV and the slope floor above, this is bounded below 2^40.
  return {EstimateKind::Remaining, seconds > 0 ? static_cast<uint64_t>(seconds) : 0};
}
} // namespace battery
} // namespace ui
