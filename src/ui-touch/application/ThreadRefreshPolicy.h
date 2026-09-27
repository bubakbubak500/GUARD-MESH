// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace ui {
// UI-thread directory invalidation. Explicit events/count changes are immediate;
// a one-minute backstop repairs mutations from older callers without events.
class ThreadRefreshPolicy {
public:
  static const uint32_t BackstopMs = 60000;
  void request() { _pending = true; }
  bool due(uint32_t now, int contacts) const {
    return _pending || contacts != _contacts || static_cast<int32_t>(now - _deadline) >= 0;
  }
  void completed(uint32_t now, int contacts) {
    _pending = false;
    _contacts = contacts;
    _deadline = now + BackstopMs;
    ++_passes;
  }
  uint32_t passes() const { return _passes; }
private:
  bool _pending = true;
  int _contacts = -1;
  uint32_t _deadline = 0;
  uint32_t _passes = 0;
};
}
