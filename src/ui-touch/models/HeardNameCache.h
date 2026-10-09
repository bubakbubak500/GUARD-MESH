// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
// Fixed-capacity identity index with an intrusive LRU. Owned by the UI loop.
class HeardNameCache {
public:
  static constexpr unsigned MaxCapacity = 1024;
  struct Record { uint8_t key[32]; char name[32]; };
  ~HeardNameCache();
  bool allocate(unsigned capacity, void* (*allocate)(size_t, bool), void (*release)(void*));
  bool remember(const uint8_t key[32], const char* name);
  bool lookup(const uint8_t* key, unsigned length, char* name, size_t size) const;
  void clear();
  unsigned count() const { return _count; }
  unsigned capacity() const { return _capacity; }
  unsigned snapshot(Record* records, unsigned capacity) const;
private:
  struct Entry { Record record; int16_t hashNext, previous, next; };
  unsigned bucket(const uint8_t*) const;
  void unlinkLru(int);
  void touch(int);
  Entry* _entries = nullptr;
  int16_t* _buckets = nullptr;
  unsigned _capacity = 0, _count = 0;
  int16_t _oldest = -1, _newest = -1;
  void (*_release)(void*) = nullptr;
};
}
