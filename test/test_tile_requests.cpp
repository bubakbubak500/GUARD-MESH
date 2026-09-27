// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/ui-touch/services/TileRequestLedger.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>

namespace {
using ui::TileRequestKey;
using ui::TileRequestLedger;

TileRequestKey key(uint8_t zoom, int32_t x, int32_t y) {
  TileRequestKey result;
  result.zoom = zoom;
  result.x = x;
  result.y = y;
  return result;
}

void exactIdentity() {
  TileRequestLedger ledger;
  const auto origin = key(16, 0, 0);
  const auto oldHashCollision = key(16, 1, 40503);
  // Old dedup: (z * 2654435761) ^ (x * 40503) ^ y. These two valid
  // zoom-16 tiles collide under that formula but are distinct requests.
  const uint32_t oldA = 16u * 2654435761u;
  const uint32_t oldB = (16u * 2654435761u) ^ 40503u ^ 40503u;
  assert(oldA == oldB);
  const uint32_t first = ledger.reserve(origin);
  const uint32_t collision = ledger.reserve(oldHashCollision);
  const uint32_t differentZoom = ledger.reserve(key(15, 0, 0));
  const uint32_t differentX = ledger.reserve(key(16, 2, 0));
  const uint32_t differentY = ledger.reserve(key(16, 0, 1));
  assert(first && collision && differentZoom && differentX && differentY);
  assert(ledger.reserve(origin) == 0 && ledger.reserve(oldHashCollision) == 0);
  assert(ledger.pending() == 5);
  assert(ledger.complete(first, false));
  assert(ledger.complete(collision, false));
  assert(ledger.complete(differentZoom, false));
  assert(ledger.complete(differentX, false));
  assert(ledger.complete(differentY, false));
  assert(ledger.pending() == 0);

  // A zero-valued old hash used to resemble an empty slot in a hash ring.
  const auto zero = key(0, 0, 0);
  const uint32_t zeroToken = ledger.reserve(zero);
  assert(zeroToken && ledger.seen(zero) && ledger.reserve(zero) == 0);
  assert(ledger.complete(zeroToken, false) && ledger.pending() == 0);
}

void capacityAndRollback() {
  TileRequestLedger ledger;
  uint32_t tokens[TileRequestLedger::QueueCapacity + 1] = {};
  for (unsigned i = 0; i < TileRequestLedger::QueueCapacity; ++i) {
    tokens[i] = ledger.reserve(key(12, static_cast<int32_t>(i), 30));
    assert(tokens[i]);
  }
  assert(ledger.pending() == TileRequestLedger::QueueCapacity);
  assert(ledger.reserve(key(12, 0, 30)) == 0);
  // One dequeued worker can coexist with all 64 queued requests.
  tokens[TileRequestLedger::QueueCapacity] = ledger.reserve(key(12, 100, 30));
  assert(tokens[TileRequestLedger::QueueCapacity]);
  assert(ledger.reserve(key(12, 101, 30)) == 0);

  const uint32_t rolled = tokens[10];
  assert(ledger.rollback(rolled));
  assert(!ledger.rollback(rolled));
  assert(ledger.pending() == TileRequestLedger::QueueCapacity);
  assert(!ledger.seen(key(12, 10, 30)));
  tokens[10] = ledger.reserve(key(12, 10, 30));
  assert(tokens[10] && tokens[10] != rolled);
  assert(!ledger.complete(rolled, true));
  assert(ledger.seen(key(12, 10, 30)));
  for (unsigned i = 0; i < TileRequestLedger::QueueCapacity + 1; ++i)
    assert(ledger.complete(tokens[i], false));
  assert(ledger.pending() == 0);
}

void completionPolicyAndImmediateWorker() {
  TileRequestLedger ledger;
  const auto success = key(8, 10, 20);
  const auto permanent = key(8, 11, 20);
  const auto transient = key(8, 12, 20);
  const uint32_t successToken = ledger.reserve(success);
  const uint32_t permanentToken = ledger.reserve(permanent);
  const uint32_t transientToken = ledger.reserve(transient);
  assert(successToken && permanentToken && transientToken);
  assert(ledger.complete(successToken, false));
  assert(ledger.complete(permanentToken, false));
  assert(ledger.complete(transientToken, true));
  assert(ledger.seen(success) && ledger.seen(permanent) && !ledger.seen(transient));
  assert(ledger.reserve(success) == 0 && ledger.reserve(permanent) == 0);
  const uint32_t retry = ledger.reserve(transient);
  assert(retry && retry != transientToken);
  assert(ledger.pending() == 1);
  assert(ledger.complete(retry, false) && ledger.pending() == 0);

  // The worker can finish before the queue publisher returns to its caller.
  const auto fast = key(9, 9, 9);
  const uint32_t fastToken = ledger.reserve(fast);
  assert(fastToken);
  assert(ledger.complete(fastToken, false));
  assert(!ledger.rollback(fastToken));
  assert(!ledger.complete(fastToken, false));
  assert(ledger.pending() == 0 && ledger.seen(fast));
  assert(ledger.reserve(fast) == 0);
}

void invalidationKeepsActive() {
  TileRequestLedger ledger;
  const auto clearKey = key(7, 3, 4);
  const uint32_t beforeClear = ledger.reserve(clearKey);
  assert(beforeClear && ledger.pending() == 1);
  ledger.clearRecent();
  assert(ledger.pending() == 1 && ledger.seen(clearKey));
  assert(ledger.reserve(clearKey) == 0);
  assert(ledger.complete(beforeClear, false));
  assert(!ledger.seen(clearKey));
  const uint32_t afterClear = ledger.reserve(clearKey);
  assert(afterClear && afterClear != beforeClear);
  assert(!ledger.complete(beforeClear, true));
  assert(ledger.seen(clearKey) && ledger.pending() == 1);
  assert(ledger.complete(afterClear, false) && ledger.pending() == 0);

  const auto forgotten = key(7, 5, 6);
  const uint32_t beforeForget = ledger.reserve(forgotten);
  assert(beforeForget);
  ledger.forget(forgotten);
  assert(ledger.pending() == 1 && ledger.seen(forgotten));
  assert(ledger.reserve(forgotten) == 0);
  assert(ledger.complete(beforeForget, false));
  assert(!ledger.seen(forgotten));
  const uint32_t afterForget = ledger.reserve(forgotten);
  assert(afterForget && afterForget != beforeForget);
  assert(!ledger.rollback(beforeForget));
  assert(ledger.seen(forgotten) && ledger.pending() == 1);
  assert(ledger.rollback(afterForget) && ledger.pending() == 0);
  assert(!ledger.seen(forgotten));
}

void recentSlotReplacement() {
  TileRequestLedger ledger;
  const auto oldKey = key(10, 0, 0);
  const uint32_t oldToken = ledger.reserve(oldKey);
  uint32_t newer[TileRequestLedger::RecentCapacity] = {};
  for (unsigned i = 0; i < TileRequestLedger::RecentCapacity; ++i) {
    newer[i] = ledger.reserve(key(10, static_cast<int32_t>(i + 1), 0));
    assert(newer[i]);
  }
  const auto newestKey = key(10, TileRequestLedger::RecentCapacity, 0);
  assert(ledger.complete(newer[TileRequestLedger::RecentCapacity - 1], false));
  newer[TileRequestLedger::RecentCapacity - 1] = 0;
  assert(ledger.rollback(oldToken));
  assert(ledger.seen(newestKey) && ledger.reserve(newestKey) == 0);
  for (unsigned i = 0; i + 1 < TileRequestLedger::RecentCapacity; ++i)
    assert(ledger.complete(newer[i], false));
  assert(ledger.pending() == 0);

  // The same slot must also survive an old retryable completion.
  ledger.clearRecent();
  const uint32_t oldAgain = ledger.reserve(oldKey);
  assert(oldAgain);
  for (unsigned i = 0; i < TileRequestLedger::RecentCapacity; ++i) {
    newer[i] = ledger.reserve(key(11, static_cast<int32_t>(i + 1), 0));
    assert(newer[i]);
  }
  const auto nextNewest = key(11, TileRequestLedger::RecentCapacity, 0);
  assert(ledger.complete(newer[TileRequestLedger::RecentCapacity - 1], false));
  newer[TileRequestLedger::RecentCapacity - 1] = 0;
  assert(ledger.complete(oldAgain, true));
  assert(ledger.seen(nextNewest) && ledger.reserve(nextNewest) == 0);
  for (unsigned i = 0; i + 1 < TileRequestLedger::RecentCapacity; ++i)
    assert(ledger.complete(newer[i], false));
  assert(ledger.pending() == 0);
}

void recentEvictionWithActiveQueue() {
  TileRequestLedger ledger;
  uint32_t tokens[TileRequestLedger::QueueCapacity] = {};
  for (unsigned i = 0; i < TileRequestLedger::QueueCapacity; ++i) {
    tokens[i] = ledger.reserve(key(13, static_cast<int32_t>(i), 17));
    assert(tokens[i]);
  }
  assert(ledger.pending() == TileRequestLedger::QueueCapacity);
  // The oldest 16 marks have rolled off the 48-entry recent ring, but their
  // queue/worker reservations must still prevent a duplicate request.
  for (unsigned i = 0; i < TileRequestLedger::QueueCapacity - TileRequestLedger::RecentCapacity; ++i)
    assert(ledger.seen(key(13, static_cast<int32_t>(i), 17)) &&
           ledger.reserve(key(13, static_cast<int32_t>(i), 17)) == 0);
  assert(ledger.complete(tokens[0], false));
  tokens[0] = 0;
  assert(!ledger.seen(key(13, 0, 17)));
  const uint32_t retryEvicted = ledger.reserve(key(13, 0, 17));
  assert(retryEvicted);
  for (unsigned i = 1; i < TileRequestLedger::QueueCapacity; ++i)
    assert(ledger.complete(tokens[i], false));
  assert(ledger.complete(retryEvicted, false));
  assert(ledger.pending() == 0);
}
} // namespace

void tileRequestsRegression() {
  exactIdentity();
  capacityAndRollback();
  completionPolicyAndImmediateWorker();
  invalidationKeepsActive();
  recentSlotReplacement();
  recentEvictionWithActiveQueue();
}
