// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
namespace ui {
namespace theme {
class TouchTheme {
public:
  TouchTheme() = default;
  TouchTheme(const TouchTheme &) = delete;
  TouchTheme &operator=(const TouchTheme &) = delete;
  void install(lv_disp_t *);

private:
  lv_theme_t _theme{};
};
} // namespace theme
} // namespace ui
