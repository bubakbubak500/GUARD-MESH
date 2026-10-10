// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
namespace gps {
struct Snapshot {
  bool enabled = false, fix = false;
  int satellites = 0, altitude = 0;
  double latitude = 0, longitude = 0;
  // Optional board diagnostics. Strings are borrowed for this snapshot only.
  const char *receiver = nullptr, *power = nullptr;
};
struct Labels {
  const char *off, *searching, *fix, *coldStart;
};
// UI-thread state, shared by the settings page and control centre. Explicit
// acquisition start keeps the elapsed time independent of opening a page.
class Status {
public:
  void acquisition(bool enabled, uint32_t now);
  void format(const Snapshot &, uint32_t now, bool compact, Labels, char *, size_t);

private:
  bool _searching = false;
  uint32_t _since = 0;
};
} // namespace gps
} // namespace ui
