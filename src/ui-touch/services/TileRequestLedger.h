// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace ui {

struct TileRequestKey {
  uint8_t zoom;
  int32_t x;
  int32_t y;
  TileRequestKey(uint8_t z = 0, int32_t tileX = 0, int32_t tileY = 0)
    : zoom(z), x(tileX), y(tileY) {}
};

// Exact, bounded bookkeeping for a 64-item tile queue. All calls must be
// serialized by the caller; SharedNetworkExecutor uses a short portMUX section.
// No queue operation or network/filesystem callback belongs under that lock.
class TileRequestLedger {
public:
  static constexpr unsigned QueueCapacity = 64;
  static constexpr unsigned ActiveCapacity = QueueCapacity + 1; // 64 waiting + 1 in flight
  static constexpr unsigned RecentCapacity = 48;

  // Returns a nonzero reservation token, or zero for an active/recent duplicate
  // or a full active ledger. Reserve BEFORE publishing to the worker queue.
  uint32_t reserve(const TileRequestKey &key);
  // If queue publication fails, undo only this reservation and its matching
  // recent mark. Safe even if newer activity has replaced the recent slot.
  bool rollback(uint32_t token);
  // Called once for a dequeued request. Retryable failure drops only this
  // token's mark; success/permanent failure retains the recent dedup mark.
  bool complete(uint32_t token, bool retryable);
  bool seen(const TileRequestKey &key) const;
  void forget(const TileRequestKey &key); // UI cache invalidation; active stays active
  void clearRecent();                     // style/backend invalidation; pending stays
  uint16_t pending() const { return _pending; }

private:
  struct Entry { TileRequestKey key{}; uint32_t token = 0; };
  static bool same(const TileRequestKey &, const TileRequestKey &);
  bool retire(uint32_t token, bool forgetRecent);
  Entry _active[ActiveCapacity]{};
  Entry _recent[RecentCapacity]{};
  unsigned _head = 0;
  uint16_t _pending = 0;
  uint32_t _nextToken = 0;
};

} // namespace ui
