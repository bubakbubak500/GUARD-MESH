// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/BatteryHistory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
void batteryHistoryRegression() {
  using namespace ui::battery;
  Sample sample{123, 456, 789, 10};
  assert(parseSample("1700000000\t2023-11-14 22:13\t4200\t100", sample));
  assert(sample.epoch == 1700000000 && sample.millivolts == 4200 && sample.cpuMHz == 0);
  assert(parseSample("0\t----------------\t3800\t50\t240", sample));
  assert(sample.epoch == 0 && sample.cpuMHz == 240);
  for (const char *bad : {"", "x\tdate\t4000\t50", "4294967296\tdate\t4000\t50", "1\t\t4000\t50",
                          "1\tdate\t0\t50", "1\tdate\t65536\t50", "1\tdate\t4000\t101",
                          "1\tdate\t4000\t50\t65536", "1\tdate\t4000\t50x", "1\tdate\t4000\t50\t80\textra"}) {
    const auto before = sample;
    assert(!parseSample(bad, sample));
    assert(sample.epoch == before.epoch && sample.millivolts == before.millivolts);
  }
  assert(!parseSample(nullptr, sample));
  Sample series[] = {{1700000000, 4200, 80, 100},
                     {1700000900, 4100, 80, 90},
                     {1700001800, 4000, 80, 80},
                     {1700002700, 3900, 80, 70}};
  auto remaining = estimate(series, 4, 4200);
  assert(remaining.kind == EstimateKind::Remaining && remaining.seconds >= 5399 && remaining.seconds <= 5401);
  assert(estimate(series, 2, 4200).kind == EstimateKind::Gathering);
  series[3].epoch = 0;
  assert(estimate(series, 4, 4200).kind == EstimateKind::Gathering);
  series[3].epoch = 1700000000;
  assert(estimate(series, 4, 4200).kind == EstimateKind::Gathering);
  series[3].epoch = 1700002700;
  series[3].millivolts = 4500;
  assert(estimate(series, 4, 4200).kind == EstimateKind::Gathering);
  series[3].millivolts = 4025;
  assert(estimate(series, 4, 4200).kind == EstimateKind::Gathering);
  for (auto &point : series)
    point.millivolts = 3800;
  assert(estimate(series, 4, 4200).kind == EstimateKind::Steady);
  series[0].millivolts = 3350;
  series[1].millivolts = 3300;
  series[2].millivolts = 3250;
  series[3].millivolts = 3200;
  remaining = estimate(series, 4, 4200);
  assert(remaining.kind == EstimateKind::Remaining && remaining.seconds == 0);
  assert(estimate(nullptr, 4, 4200).kind == EstimateKind::Gathering);
}
