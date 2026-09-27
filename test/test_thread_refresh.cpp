// SPDX-License-Identifier: GPL-3.0-or-later
#undef NDEBUG
#include <cassert>
#include "ui-touch/application/ThreadRefreshPolicy.h"
int main() {
  ui::ThreadRefreshPolicy policy;
  assert(policy.due(0, 500));
  policy.completed(0, 500);
  for (uint32_t tick = 1; tick < 60000; ++tick) assert(!policy.due(tick, 500));
  assert(policy.due(60000, 500));
  policy.completed(60000, 500);
  assert(policy.due(60001, 501));
  policy.completed(60001, 501);
  assert(policy.due(60002, 500));
  policy.completed(60002, 500);
  policy.request(); policy.request(); policy.request();
  assert(policy.due(60003, 500));
  policy.completed(60003, 500);
  assert(!policy.due(60004, 500) && policy.passes() == 5);
  policy.completed(UINT32_MAX - 999, 500);
  assert(!policy.due(UINT32_MAX - 998, 500));
  assert(!policy.due(58999, 500));
  assert(policy.due(59000, 500));
}
