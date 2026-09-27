// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/ui-touch/models/KeyBindings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <climits>
#include <cstring>
#include <initializer_list>
void keyBindingsRegression() {
  using Keys = ui::KeyBindings;
  using Result = Keys::Result;
  Keys keys, extra(true);
  for (unsigned i = 0; i < Keys::Count; ++i) {
    assert(*Keys::name(i));
    assert(keys.validate(i, keys.key(i)) == Result::Ok);
  }
  assert(!*Keys::name(Keys::Count) && !keys.key(UINT_MAX));
  assert(keys.tab('E') == 0 && keys.tab('I') == 4 && keys.tab('W') == -1);
  assert(keys.direction('W') == 0 && keys.direction('C') == 7 && keys.direction('E') == -1);
  assert(extra.direction('X') == 1 && extra.direction('V') == 7 && extra.direction(0) == -1);
  assert(!extra.key(10));
  for (int key : {8, 27, 127})
    assert(keys.validate(0, key) == Result::Cancelled);
  for (int key : {-1, 0, 48, 91, 256})
    assert(keys.validate(0, key) == Result::Invalid);
  assert(keys.validate(UINT_MAX, 'b') == Result::OutOfRange);
  assert(keys.validate(0, 'W') == Result::Duplicate);
  assert(keys.validate(5, 'E') == Result::Duplicate);
  assert(keys.validate(0, 'B') == Result::Ok);
  keys.restore(0, 'B');
  keys.restore(0, 0);
  keys.restore(UINT_MAX, 'h');
  assert(keys.key(0) == 'b' && keys.tab('B') == 0 && keys.tab('e') == -1);
  assert(Keys::backlightLevel(0) == 0 && Keys::backlightLevel(1) == 1);
  assert(Keys::backlightLevel(25) == 16 && Keys::backlightLevel(50) == 64);
  assert(Keys::backlightLevel(100) == 255 && Keys::backlightLevel(UINT_MAX) == 255);
  for (unsigned p = 1; p <= 100; ++p)
    assert(Keys::backlightLevel(p) >= Keys::backlightLevel(p - 1));
}
