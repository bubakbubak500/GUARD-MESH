// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
namespace ui {
// UI-thread clipboard. Owns its bytes; no LVGL, allocation, or notification policy.
class TextClipboard {
public:
  static constexpr size_t Capacity = 640;
  // Optional LVGL recolor syntax: strip commands and decode escaped hashes.
  // Truncation never leaves a partial UTF-8 character. Null leaves the value intact.
  bool set(const char *text, bool recolor = false);
  const char *text() const { return _text; }

private:
  char _text[Capacity]{};
};
} // namespace ui
