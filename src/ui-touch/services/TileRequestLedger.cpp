// SPDX-License-Identifier: GPL-3.0-or-later
#include "TileRequestLedger.h"

namespace ui {

bool TileRequestLedger::same(const TileRequestKey &a, const TileRequestKey &b) {
  return a.zoom == b.zoom && a.x == b.x && a.y == b.y;
}

bool TileRequestLedger::seen(const TileRequestKey &key) const {
  for (const auto &entry : _active)
    if (entry.token && same(entry.key, key)) return true;
  for (const auto &entry : _recent)
    if (entry.token && same(entry.key, key)) return true;
  return false;
}

uint32_t TileRequestLedger::reserve(const TileRequestKey &key) {
  if (seen(key)) return 0;
  Entry *freeEntry = nullptr;
  for (auto &entry : _active)
    if (!entry.token) { freeEntry = &entry; break; }
  if (!freeEntry) return 0;
  // A 32-bit wrap must not recycle a token still held by the queue/worker.
  uint32_t token;
  for (;;) {
    token = ++_nextToken;
    if (!token) continue;
    bool collision = false;
    for (const auto &entry : _active)
      if (entry.token == token) { collision = true; break; }
    if (!collision)
      for (const auto &entry : _recent)
        if (entry.token == token) { collision = true; break; }
    if (!collision) break;
  }
  freeEntry->key = key;
  freeEntry->token = token;
  _recent[_head].key = key;
  _recent[_head].token = token;
  _head = (_head + 1) % RecentCapacity;
  ++_pending;
  return token;
}

bool TileRequestLedger::retire(uint32_t token, bool forgetRecent) {
  if (!token) return false;
  Entry *active = nullptr;
  for (auto &entry : _active)
    if (entry.token == token) { active = &entry; break; }
  if (!active) return false; // duplicate completion cannot decrement twice
  active->token = 0;
  --_pending;
  if (forgetRecent)
    for (auto &entry : _recent)
      if (entry.token == token) entry.token = 0;
  return true;
}

bool TileRequestLedger::rollback(uint32_t token) { return retire(token, true); }
bool TileRequestLedger::complete(uint32_t token, bool retryable) {
  return retire(token, retryable);
}
void TileRequestLedger::forget(const TileRequestKey &key) {
  for (auto &entry : _recent)
    if (entry.token && same(entry.key, key)) entry.token = 0;
}
void TileRequestLedger::clearRecent() {
  for (auto &entry : _recent) entry.token = 0;
  _head = 0;
}

} // namespace ui
