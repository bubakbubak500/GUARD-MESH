// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/ui-touch/models/ColorChoice.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
void colorChoiceRegression() {
  using namespace ui::colorChoice;
  uint32_t color = 123;
  const char *invalid[] = {nullptr,  "",       "0",      "ABCDE",  "1234567",
                           "GG0000", "#12345", " 12345", "12345 ", "12345z"};
  for (auto *text : invalid)
    assert(!parseHex(text, color) && color == 123);
  assert(parseHex("000000", color) && color == 0);
  assert(parseHex("ffffff", color) && color == 0xFFFFFF);
  assert(parseHex("AbCdEf", color) && color == 0xABCDEF);
  for (unsigned i = 0; i < AccentCount; ++i) {
    char text[8];
    snprintf(text, sizeof text, "%06X", unsigned(accent(i)));
    assert(parseHex(text, color) && color == accent(i));
    for (unsigned j = i + 1; j < AccentCount; ++j)
      assert(accent(i) != accent(j));
  }
  assert(accent(0) == DefaultAccent && accent(999) == DefaultAccent);
  assert(lock(0) == 0xE6F2FF && lock(999) == lock(0));
}
