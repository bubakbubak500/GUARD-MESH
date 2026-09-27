// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
namespace battery {
struct Sample {
  uint32_t epoch;
  uint16_t millivolts, cpuMHz;
  int percent;
};
constexpr size_t HistoryCapacity = 288;
constexpr uint32_t HistorySeconds = 24u * 60u * 60u;
// Accepts the existing four-column and five-column tab-separated log format.
bool parseSample(const char *, Sample &);
enum class EstimateKind { Gathering, Unavailable, Steady, Remaining };
struct Estimate {
  EstimateKind kind;
  uint64_t seconds;
};
Estimate estimate(const Sample *, size_t count, uint16_t fullMv);
} // namespace battery
} // namespace ui
