// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/MessageStore.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Store = ui::MessageStore;
static void *allocate(size_t bytes, bool) { return std::malloc(bytes); }
static Store::UIMessage record(const char *text = "message") {
  Store::UIMessage value{};
  std::snprintf(value.text, sizeof value.text, "%s", text);
  return value;
}
// Independent oracle: walk arrival order, including empty-text history records.
static void verify(Store &store) {
  const auto ring = store.ring();
  std::vector<int> indexes(store.capacity());
  for (int t = 0; t < Store::MAX_UI_THREADS; ++t) {
    const auto &thread = store.thread(t);
    const Store::UIMessage *last = nullptr;
    int count = 0;
    if (thread.used) for (int i = 0; i < ring.count; ++i) {
      const auto &m = ring.records[(ring.head - 1 - i + ring.capacity) % ring.capacity];
      if (!m.thread[0] || m.channel != thread.channel || std::strcmp(m.thread, thread.name)) continue;
      ++count;
      if (!last && m.text[0]) last = &m;
    }
    assert(store.threadHasMessageHistory(t) == (count > 0));
    assert(store.getThreadMessageIndexes(t, indexes.data(), store.capacity(), true) == count);
    Store::UIMessage out{};
    for (int repeat = 0; repeat < 2; ++repeat) {
      assert(store.lastThreadMessage(t, out) == (last != nullptr));
      if (last) assert(out.seq == last->seq && out.ts == last->ts && out.deliv_state == last->deliv_state &&
                       !std::strcmp(out.text, last->text) && !std::strcmp(out.thread, last->thread));
    }
  }
}
static Store *observed;
static void observe(uint32_t) { verify(*observed); }
static void changes() {
  ui::MessageChanges journal;
  ui::MessageChanges::Item items[ui::MessageChanges::Capacity];
  int count;
  uint32_t current;
  const uint32_t initial = journal.revision();
  journal.record(3, 17);
  assert(journal.since(initial, items, 32, count, current) && count == 1 && items[0].slot == 3 && items[0].sequence == 17);
  assert(journal.since(initial, items, 32, count, current) && count == 1); // non-destructive
  assert(!journal.since(initial, nullptr, 0, count, current));
  const uint32_t beforeReset = current;
  journal.reset();
  assert(!journal.since(beforeReset, items, 32, count, current) && count == 0);
  const uint32_t afterReset = current;
  assert(journal.since(afterReset, nullptr, 0, count, current) && count == 0);
  for (int i = 0; i < 32; ++i) journal.record(i, i + 1);
  assert(journal.since(afterReset, items, 32, count, current) && count == 32);
  journal.record(9, 99);
  assert(!journal.since(afterReset, items, 32, count, current) && count == 0);
  Store store; assert(store.allocate(3, allocate, std::free));
  int thread = store.findOrCreateThread("journal", false, 0);
  auto m = record(); m.outgoing = true; m.ack_hash = 42;
  assert(store.append(thread, m, false, false, 0, nullptr, nullptr));
  const uint32_t appended = store.changes().revision();
  assert(store.acknowledge(42, nullptr));
  assert(store.changes().since(appended, items, 32, count, current) && count == 1 && items[0].slot == 0 && items[0].sequence == 1);
  const uint32_t acknowledged = current;
  assert(store.setMessageTimestamp(0, 1, 123, nullptr));
  assert(!store.changes().since(acknowledged, items, 32, count, current));
  std::puts("Message change journal: independent cursors, reset, exact capacity, overflow and ACK/time publication PASS");
}
static void mutations() {
  Store store;
  assert(store.allocate(7, allocate, std::free));
  observed = &store;
  int a = store.findOrCreateThread("same", false, 0);
  int b = store.findOrCreateThread("same", true, 0);
  int c = store.findOrCreateThread("other", false, 0);
  verify(store);
  for (int i = 0; i < 50; ++i) {
    auto m = record(i % 7 == 0 ? "" : "text");
    m.ts = 100 - i; // arrival order must win over wall-clock order
    assert(store.append(i % 3 == 0 ? b : a, m, false, false, 0, observe, observe));
    verify(store);
    if (i % 4 == 0) {
      const auto ring = store.ring();
      assert(store.deleteMessageBySlot((ring.head + ring.capacity - 1) % ring.capacity, observe));
      verify(store);
    }
  }
  // Cap callbacks may query both derived indexes after each tombstone.
  for (int i = 0; i < 20; ++i) {
    assert(store.append(a, record(), false, false, 2, observe, observe));
    verify(store);
  }
  assert(store.renameThread(a, "renamed", observe)); verify(store);
  store.clearThreadHistory(a, observe); verify(store);
  assert(store.append(c, record("old preview"), false, false, 0, observe, observe));
  assert(store.append(c, record(""), false, false, 0, observe, observe)); verify(store);
  int purged;
  assert(store.removeThread(c, observe, &purged)); verify(store);
  assert(store.findOrCreateThread("reused", false, 0) == c); verify(store);
  for (int i = 0; i < 9; ++i) assert(store.append(c, record(), false, false, 0, observe, observe));
  verify(store);
  store.resequenceMessages(); verify(store);
  auto m = record("ack"); m.outgoing = true; m.ack_hash = 123; m.deliv_state = Store::DELIV_SENT;
  assert(store.append(c, m, false, false, 0, observe, observe)); verify(store);
  assert(store.acknowledge(123, observe)); verify(store);
  auto ring = store.ring();
  int slot = (ring.head + ring.capacity - 1) % ring.capacity;
  assert(store.setMessageTimestamp(slot, ring.records[slot].seq, 999, observe)); verify(store);
  Store::UIMessage copy;
  assert(store.lastThreadMessage(c, copy)); copy.text[0] = 0; verify(store);
  // Warm caches, reset, then expose a rotated restore with an orphan identity.
  store.resetMessages(); verify(store);
  m = record("restored"); std::strcpy(m.thread, "orphan"); m.seq = 0;
  assert(store.restoreMessage(4, m)); verify(store); // staged record remains invisible
  assert(store.restoreRingState(7, 5, UINT32_MAX)); verify(store);
  int orphan = store.findOrCreateThread("orphan", false, 0); verify(store);
  assert(orphan >= 0 && store.lastThreadMessage(orphan, copy) && copy.seq == 0);
  assert(store.append(orphan, record(), false, false, 0, observe, observe)); verify(store);
  assert(!store.append(orphan, record(), false, false, 0, observe, observe)); verify(store);
  store.resequenceMessages(); verify(store);
  store.resetMessages();
  for (int i = 0; i < 20; ++i) {
    m = record(); std::strcpy(m.thread, "renamed"); m.seq = i + 1;
    assert(store.restoreMessage(i % 7, m));
  }
  assert(store.finishChronologicalRestore(20, 21)); verify(store);
  std::puts("Message indexes: rollover, tombstones, empty previews, query callbacks, cap, rename/reuse, restore, ACK/time and sequence boundaries PASS");
}
static void benchmark(int capacity, bool baseline, bool quietThreads) {
  Store store;
  assert(store.allocate(capacity, allocate, std::free));
  int active = store.findOrCreateThread("active", false, 0);
  for (int i = 1; i < Store::MAX_UI_THREADS; ++i) {
    char name[24]; std::snprintf(name, sizeof name, "empty-channel-%d", i);
    assert(store.findOrCreateThread(name, true, 0) == i);
  }
  int filled = 0;
  if (quietThreads) for (int i = 1; i < Store::MAX_UI_THREADS; ++i) {
    assert(store.append(i, record(), false, false, 0, nullptr, nullptr));
    ++filled;
  }
  for (int i = filled; i < capacity; ++i) assert(store.append(active, record(), false, false, 0, nullptr, nullptr));
  Store::UIMessage out;
  for (int t = 0; t < Store::MAX_UI_THREADS; ++t) {
    store.threadHasMessageHistory(t); store.lastThreadMessage(t, out);
  }
  ui::messageStoreWork = {};
  uint64_t checksum = 0;
  constexpr int iterations = 40;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < iterations; ++i) {
    assert(store.append(active, record(), false, false, capacity, nullptr, nullptr));
    for (int t = 0; t < Store::MAX_UI_THREADS; ++t) {
      checksum += store.threadHasMessageHistory(t);
      if (store.lastThreadMessage(t, out)) checksum += out.seq;
    }
  }
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  const auto &work = ui::messageStoreWork;
  std::printf("PERF scenario=%s capacity=%d iterations=%d rebuilds=%llu history_records=%llu thread_comparisons=%llu preview_records=%llu time_us=%lld checksum=%llu\n",
              quietThreads ? "quiet" : "empty", capacity, iterations, (unsigned long long)work.historyRebuilds, (unsigned long long)work.historyRecords,
              (unsigned long long)work.historyComparisons, (unsigned long long)work.latestRecords,
              (long long)us, (unsigned long long)checksum);
  if (!baseline) assert(work.historyRebuilds == 0 && work.historyRecords == 0 && work.latestRecords == 0);
  assert(checksum != 0);
}
int main(int argc, char **) {
  const bool baseline = argc > 1;
  if (!baseline) { changes(); mutations(); }
  benchmark(500, baseline, false);
  benchmark(5000, baseline, false);
  benchmark(500, baseline, true);
  benchmark(5000, baseline, true);
}
