// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeardNameCache.h"
#include <cstring>
namespace ui {
HeardNameCache::~HeardNameCache() {
  if (_release) { _release(_entries); _release(_buckets); }
}
bool HeardNameCache::allocate(unsigned capacity, void* (*alloc)(size_t, bool), void (*release)(void*)) {
  if (_entries) return true;
  if (!alloc || !release || !capacity || capacity > MaxCapacity || (capacity & (capacity - 1))) return false;
  auto* entries = static_cast<Entry*>(alloc(sizeof(Entry) * capacity, true));
  auto* buckets = static_cast<int16_t*>(alloc(sizeof(int16_t) * capacity * 2, true));
  if (!entries || !buckets) {
    release(entries); release(buckets);
    entries = static_cast<Entry*>(alloc(sizeof(Entry) * capacity, false));
    buckets = static_cast<int16_t*>(alloc(sizeof(int16_t) * capacity * 2, false));
  }
  if (!entries || !buckets) { release(entries); release(buckets); return false; }
  _entries = entries; _buckets = buckets; _capacity = capacity; _release = release;
  clear(); return true;
}
unsigned HeardNameCache::bucket(const uint8_t* key) const {
  uint32_t hash = 2166136261u;
  // Prefix replies share this index; full identities are always compared below.
  for (unsigned i = 0; i < 8; ++i) hash = (hash ^ key[i]) * 16777619u;
  return hash & (_capacity * 2 - 1);
}
void HeardNameCache::clear() {
  if (_buckets) memset(_buckets, 0xff, sizeof(int16_t) * _capacity * 2);
  _count = 0; _oldest = _newest = -1;
}
void HeardNameCache::unlinkLru(int slot) {
  auto& e = _entries[slot];
  if (e.previous >= 0) _entries[e.previous].next = e.next; else _oldest = e.next;
  if (e.next >= 0) _entries[e.next].previous = e.previous; else _newest = e.previous;
}
void HeardNameCache::touch(int slot) {
  auto& e = _entries[slot]; e.previous = _newest; e.next = -1;
  if (_newest >= 0) _entries[_newest].next = slot; else _oldest = slot;
  _newest = slot;
}
bool HeardNameCache::remember(const uint8_t key[32], const char* name) {
  if (!_entries || !key || !name || !name[0]) return false;
  const unsigned hash = bucket(key);
  int slot = _buckets[hash];
  while (slot >= 0 && memcmp(_entries[slot].record.key, key, 32)) slot = _entries[slot].hashNext;
  char bounded[32] = {};
  unsigned length = 0;
  while (length < 31 && name[length]) ++length;
  // Never persist a partial UTF-8 codepoint when the advert fills its field.
  if (length == 31 && (static_cast<uint8_t>(name[length]) & 0xc0) == 0x80)
    while (length && (static_cast<uint8_t>(name[length]) & 0xc0) == 0x80) --length;
  memcpy(bounded, name, length);
  bool changed = slot < 0 || memcmp(_entries[slot].record.name, bounded, sizeof bounded);
  if (slot >= 0) unlinkLru(slot);
  else {
    if (_count < _capacity) slot = _count++;
    else {
      slot = _oldest;
      int16_t* link = &_buckets[bucket(_entries[slot].record.key)];
      while (*link >= 0 && *link != slot) link = &_entries[*link].hashNext;
      if (*link == slot) *link = _entries[slot].hashNext;
      unlinkLru(slot);
    }
    memcpy(_entries[slot].record.key, key, 32);
    _entries[slot].hashNext = _buckets[hash]; _buckets[hash] = slot;
  }
  memcpy(_entries[slot].record.name, bounded, 32); touch(slot);
  return changed;
}
bool HeardNameCache::lookup(const uint8_t* key, unsigned length, char* name, size_t size) const {
  if (name && size) name[0] = '\0';
  if (!_entries || !key || !name || !size || (length != 8 && length != 32)) return false;
  int found = -1;
  for (int slot = _buckets[bucket(key)]; slot >= 0; slot = _entries[slot].hashNext) {
    if (memcmp(_entries[slot].record.key, key, length)) continue;
    if (found >= 0) return false; // ambiguous prefix
    found = slot;
  }
  if (found < 0) return false;
  size_t lengthName = strlen(_entries[found].record.name);
  if (lengthName >= size) lengthName = size - 1;
  memcpy(name, _entries[found].record.name, lengthName); name[lengthName] = '\0';
  return true;
}
int HeardNameCache::resolve(const uint8_t* key, unsigned length, uint8_t fullKey[32]) const {
  if (!_entries || !key || !fullKey || (length != 8 && length != 32)) return 0;
  int found = -1;
  for (int slot = _buckets[bucket(key)]; slot >= 0; slot = _entries[slot].hashNext) {
    if (memcmp(_entries[slot].record.key, key, length)) continue;
    if (found >= 0) return -1;
    found = slot;
  }
  if (found < 0) return 0;
  memcpy(fullKey, _entries[found].record.key, 32);
  return 1;
}
unsigned HeardNameCache::snapshot(Record* records, unsigned capacity) const {
  unsigned count = 0;
  for (int slot = _oldest; slot >= 0 && count < capacity; slot = _entries[slot].next)
    records[count++] = _entries[slot].record;
  return count;
}
}
