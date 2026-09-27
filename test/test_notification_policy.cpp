// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/NotificationPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <climits>
#include <cstring>
void notificationPolicyRegression() {
  using namespace ui::notification;
  assert(!quietWindow(false, 0, 44, 12) && !quietWindow(true, -1, 44, 12));
  assert(!quietWindow(true, 1440, 44, 12) && !quietWindow(true, 60, 48, 0));
  assert(!quietWindow(true, 60, 0, 48));
  assert(quietWindow(true, 1320, 44, 12) && quietWindow(true, 0, 44, 12));
  assert(quietWindow(true, 359, 44, 12) && !quietWindow(true, 360, 44, 12));
  assert(!quietWindow(true, 1319, 44, 12) && !quietWindow(true, 900, 44, 44));
  // Each cyclic interval has exactly its requested number of half-hour slots,
  // irrespective of midnight; both endpoints have explicit inclusion rules.
  for (unsigned start = 0; start < 48; ++start)
    for (unsigned end = 0; end < 48; ++end) {
      unsigned count = 0;
      for (int minute = 0; minute < 1440; ++minute)
        count += quietWindow(true, minute, start, end);
      assert(count == (end + 48 - start) % 48 * 30);
      assert(!quietWindow(true, end * 30, start, end));
      assert(quietWindow(true, start * 30, start, end) == (start != end));
    }
  assert(stepSlot(0, INT_MIN) == 47 && stepSlot(47, INT_MAX) == 0 && stepSlot(5, 0) == 5);
  char text[8]{};
  formatSlot(47, text, sizeof text);
  assert(!strcmp(text, "23:30"));
  formatSlot(0, text, sizeof text);
  assert(!strcmp(text, "00:00"));
  text[0] = 'x';
  formatSlot(1, text, 0);
  assert(text[0] == 'x');
  formatSlot(1, text, 1);
  assert(text[0] == 0);
  formatSlot(1, nullptr, 8);
  assert(colorMask(0) == 4 && colorMask(1) == 2 && colorMask(2) == 1);
  assert(colorMask(3) == 6 && colorMask(4) == 3 && colorMask(5) == 5 && colorMask(6) == 7);
  assert(colorRgb(0) == 0xff0000 && colorRgb(6) == 0xffffff && colorRgb(99) == colorRgb(0));
  assert(soundSlot(false, true, false, false, true, true, false) == 2);
  assert(soundSlot(false, true, true, true, true, false, true) == 0);
  assert(soundSlot(false, true, true, true, true, true, true) == -1);
  assert(soundSlot(true, false, true, false, true, false, false) == -1);
  assert(soundSlot(true, false, false, true, false, true, true) == 1);
  assert(soundSlot(false, false, true, true, true, true, false) == -1);
}
