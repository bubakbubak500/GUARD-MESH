// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../include/lv_conf.h"

// Firmware asserts stop the device. On the desktop, fail immediately and let
// the simulator crash reporter identify the caller instead of timing out.
#define LV_ASSERT_HANDLER_INCLUDE <stdio.h>
#define LV_ASSERT_HANDLER do { \
  fprintf(stderr, "LVGL assertion at %s:%d\n", __FILE__, __LINE__); \
  fflush(stderr); \
  __builtin_trap(); \
} while (0);
