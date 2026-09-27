// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
namespace accents {
struct AccentSet {
  char key;
  const char *const *v;
  uint8_t n;
};
extern const AccentSet kAccentSets[25];
const AccentSet *lookup(const char *key);
} // namespace accents
} // namespace ui
