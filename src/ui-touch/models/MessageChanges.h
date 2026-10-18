// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
// UI-thread change history. Readers have independent cursors; an old cursor or
// a structural reset requests a full check instead of losing an invalidation.
class MessageChanges {
public:
  enum { Capacity = 32 };
  struct Item { int slot; uint32_t sequence; };
  uint32_t revision() const { return _revision; }
  void record(int slot, uint32_t sequence) { push(slot, sequence); }
  void reset() { push(-1, 0); }
  bool since(uint32_t after, Item *out, int capacity, int &count, uint32_t &current) const {
    current = _revision;
    count = 0;
    if (_exhausted) return false;
    if (after == current) return true;
    int newer = 0;
    for (; newer < _count; ++newer) {
      const auto &entry = _entries[(_head + Capacity - 1 - newer) % Capacity];
      if (entry.revision == after) break;
      if (entry.item.slot < 0) return false;
    }
    if ((newer == _count && after != _base) || newer > capacity || (newer && !out)) return false;
    for (int i = newer - 1; i >= 0; --i)
      out[count++] = _entries[(_head + Capacity - 1 - i) % Capacity].item;
    return true;
  }
private:
  struct Entry { uint32_t revision; Item item; };
  void push(int slot, uint32_t sequence) {
    if (_revision == UINT32_MAX) { _exhausted = true; return; }
    ++_revision;
    if (_count == Capacity) _base = _entries[_head].revision;
    _entries[_head] = {_revision, {slot, sequence}};
    _head = (_head + 1) % Capacity;
    if (_count < Capacity) ++_count;
  }
  Entry _entries[Capacity] = {};
  uint32_t _revision = 1;
  uint32_t _base = 1;
  int _head = 0, _count = 0;
  bool _exhausted = false;
};
}
