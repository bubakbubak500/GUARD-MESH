// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include "services/HistoryCodec.h"
#include "services/HistoryService.h"
#include "platform/StorageAccess.h"
#include <stdexcept>
#include <thread>
namespace {
using ui::MessageStore;
using ui::history::HistoryService;
using namespace ui::history;
fs::FS disk;
HistoryService *current = nullptr;
void requireHistory(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void dirty(uint32_t seq) { current->segMarkSeqDirty(seq); }
struct Session {
  MessageStore messages;
  HistoryService history;
  int active = -1, counter = 0;
  bool channel = false;
  unsigned selectionReads = 0, selectionRestores = 0;
  explicit Session(bool async = false, int capacity = 600) {
    requireHistory(messages.allocate(
                       capacity, [](size_t n, bool) { return calloc(1, n); }, free),
                   "History allocation failed");
    HistoryService::Host host{[] { return &disk; }, [] { return "/meshcomod"; }, [] { return true; },
                              [] { return true; }, nullptr, nullptr, async ? +[] { return true; } : nullptr};
    host.selectionContext = this;
    host.selection = [](void* ctx) {
      auto& session = *static_cast<Session*>(ctx);
      ++session.selectionReads;
      return HistoryService::Selection(session.active, session.channel);
    };
    host.restoreSelection = [](void* ctx, HistoryService::Selection value) {
      auto& session = *static_cast<Session*>(ctx);
      ++session.selectionRestores;
      session.active = value.index;
      session.channel = value.channel;
    };
    history.configure(messages, counter, host);
    current = &history;
  }
  void append(int number) {
    const int thread = messages.findOrCreateThread("history-test", false, 10);
    ui::MessageTypes::UIMessage message{};
    strcpy(message.thread, "history-test");
    strcpy(message.sender, "test");
    snprintf(message.text, sizeof message.text, "message %d", number);
    message.ts = number + 100;
    message.outgoing = true;
    requireHistory(messages.append(thread, message, false, false, 0, nullptr, dirty),
                   "History append failed");
    active = thread;
    counter++;
  }
  int count() {
    int indexes[600];
    return messages.getThreadMessageIndexes(messages.findThreadByName("history-test", false), indexes, 600,
                                            false);
  }
};
} // namespace
void runHistoryRegression() {
  // Persistence exchanges a selection value with its owner. Malformed or stale
  // stored selections must not restore a channel mode over a direct thread.
  for (int selectionCase = 0; selectionCase < 4; ++selectionCase) {
    disk.enableMemory();
    {
      Session saved;
      saved.append(1);
      if (selectionCase == 1) saved.channel = true;
      if (selectionCase == 2) saved.active = MessageStore::MAX_UI_THREADS;
      if (selectionCase == 3) saved.active = -1;
      requireHistory(saved.history.saveThreadsToStorage() && saved.history.saveMsgsToStorage(),
                     "Selection callback fixture save failed");
      requireHistory(saved.selectionReads > 0, "History bypassed selection owner on save");
    }
    {
      Session loaded;
      requireHistory(loaded.history.loadHistoryFromStorage() && loaded.count() == 1,
                     "Selection callback fixture load failed");
      requireHistory(loaded.selectionRestores > 0 && !loaded.channel &&
                     (selectionCase == 0 ? loaded.active >= 0 : loaded.active == -1),
                     "History restored an invalid selection or bypassed its owner");
    }
  }
  disk.enableMemory();
  {
    Session first;
    for (int i = 0; i < 330; ++i)
      first.append(i);
    requireHistory(first.history.saveThreadsToStorage() && first.history.saveMsgsToStorage(),
                   "Initial segmented history save failed");
    requireHistory(first.history.status().segments == 2, "History did not cross the segment boundary");
  }
  {
    Session second;
    requireHistory(second.history.loadHistoryFromStorage() && second.count() == 330 && second.counter == 330,
                   "Saved history did not survive service recreation");
    int indexes[600];
    second.messages.getThreadMessageIndexes(second.active, indexes, 600, false);
    requireHistory(second.messages.deleteMessageBySlot(indexes[12], dirty), "History deletion failed");
    requireHistory(second.history.saveMsgsToStorage(), "History compaction failed");
  }
  {
    Session third;
    requireHistory(third.history.loadHistoryFromStorage() && third.count() == 329,
                   "Deleted message reappeared after reload");
    third.append(330);
    disk.limitWrites(sizeof(UiSegHeader) + 1);
    requireHistory(!third.history.saveMsgsToStorage(), "Short segment write reported success");
    requireHistory(third.history.status().failures > 0, "Failed write missing from diagnostics");
    disk.allowWrites();
    requireHistory(third.history.saveMsgsToStorage(), "History did not recover from short write");
  }
  {
    Session fourth;
    requireHistory(fourth.history.loadHistoryFromStorage() && fourth.count() == 330,
                   "Retry lost or duplicated history");
  }

  {
    Session renamed;
    requireHistory(renamed.history.loadHistoryFromStorage(), "Rename history setup failed");
    const int thread = renamed.messages.findThreadByName("history-test", false);
    requireHistory(renamed.messages.renameThread(thread, "renamed-peer", dirty),
                   "History peer rename failed");
    requireHistory(renamed.history.saveThreadsToStorage() && renamed.history.saveMsgsToStorage(),
                   "Renamed history did not persist");
  }
  {
    Session reloaded;
    requireHistory(reloaded.history.loadHistoryFromStorage(), "Renamed history reload failed");
    const int thread = reloaded.messages.findThreadByName("renamed-peer", false);
    int slots[600];
    requireHistory(thread >= 0 && reloaded.messages.getThreadMessageIndexes(thread, slots, 600, false) == 330,
                   "Renaming orphaned persisted message records");
    int purged = 0;
    requireHistory(reloaded.messages.removeThread(thread, dirty, &purged) && purged == 330,
                   "Model thread deletion did not purge persisted messages");
    requireHistory(reloaded.history.saveThreadsToStorage() && reloaded.history.saveMsgsToStorage(),
                   "Deleted thread failed to persist");
  }
  {
    Session removed;
    requireHistory(removed.history.loadHistoryFromStorage() &&
                       removed.messages.findThreadByName("renamed-peer", false) < 0 &&
                       removed.messages.ring().count == 0,
                   "Deleted thread/messages reappeared after reload");
  }
  disk.enableMemory();
  {
    Session large;
    for (int i = 0; i < 550; ++i)
      large.append(i);
    requireHistory(large.history.saveThreadsToStorage() && large.history.saveMsgsToStorage(),
                   "Large ring history setup failed");
  }
  {
    Session small(false, 128);
    requireHistory(small.history.loadHistoryFromStorage() && small.count() == 128,
                   "Smaller ring did not keep the newest records");
    int slots[128];
    const int thread = small.messages.findThreadByName("history-test", false);
    small.messages.getThreadMessageIndexes(thread, slots, 128, false);
    MessageStore::UIMessage oldest{}, newest{};
    requireHistory(small.messages.getMessageByIndex(slots[0], oldest) && oldest.seq == 423 &&
                       small.messages.getMessageByIndex(slots[127], newest) && newest.seq == 550,
                   "Wrapped import changed chronological order");
    small.append(550);
    requireHistory(small.messages.lastThreadMessage(thread, newest) && newest.seq == 551,
                   "Imported ring reused a persisted sequence");
  }

  // The combined legacy file uses its own physical ring size, not today's RAM capacity.
  for (int capacity : {10, 3}) {
    for (int failure : {0, 1, 2}) {
      disk.enableMemory();
      disk.mkdir("/meshcomod");
      {
        auto legacy = disk.open("/meshcomod/ui_chat_history_v1.bin", "w");
        UiHistoryHeader header{};
        header.magic = k_ui_history_magic;
        header.version = k_ui_history_version;
        header.ui_msg_count = 6;
        header.ui_msg_head = 2;
        header.msgcount = 6;
        header.thread_rec_size = sizeof(UiHistoryThread);
        header.msg_rec_size = sizeof(UiHistoryMsg);
        header.active_thread_idx = 0;
        legacy.write(reinterpret_cast<const uint8_t *>(&header), sizeof header);
        for (int i = 0; i < MessageStore::MAX_UI_THREADS; ++i) {
          UiHistoryThread thread{};
          thread.mesh_contact_idx = thread.mesh_channel_slot = -1;
          if (!i) {
            thread.used = 1;
            strcpy(thread.name, "history-test");
          }
          legacy.write(reinterpret_cast<const uint8_t *>(&thread), sizeof thread);
        }
        for (int i = 0; i < 6; ++i) {
          UiHistoryMsg message{};
          message.ts = (i - 2 + 6) % 6 + 1;
          strcpy(message.thread, "history-test");
          snprintf(message.text, sizeof message.text, "legacy %u", message.ts);
          legacy.write(reinterpret_cast<const uint8_t *>(&message), sizeof message);
        }
      }
      const int kept = capacity < 6 ? capacity : 6;
      const size_t segmentBytes = sizeof(UiSegHeader) + kept * sizeof(UiSegMsg);
      const size_t indexBytes =
          sizeof(UiHistoryHeader) + MessageStore::MAX_UI_THREADS * sizeof(UiHistoryThread);
      if (failure)
        disk.limitWrites(segmentBytes + (failure == 2 ? indexBytes : 1));
      {
        Session loaded(false, capacity);
        requireHistory(loaded.history.loadHistoryFromStorage(), "Combined legacy import failed");
        requireHistory(loaded.count() == kept,
                       "Combined legacy import used RAM ring geometry for disk slots");
        int indexes[10];
        loaded.messages.getThreadMessageIndexes(loaded.active, indexes, 10, false);
        MessageStore::UIMessage first{}, last{};
        requireHistory(loaded.messages.getMessageByIndex(indexes[0], first) &&
                           first.ts == unsigned(7 - kept) &&
                           loaded.messages.getMessageByIndex(indexes[kept - 1], last) && last.ts == 6,
                       "Combined legacy migration lost chronological order");
        requireHistory(loaded.history.status().ready == (failure == 0), "Incomplete migration was committed");
      }
      disk.allowWrites();
      {
        Session rebooted(false, capacity);
        requireHistory(rebooted.history.loadHistoryFromStorage() && rebooted.count() == kept &&
                           rebooted.counter == 6 && rebooted.history.status().ready,
                       "Legacy migration lost its thread index or source after reboot");
        MessageStore::UIMessage last{};
        requireHistory(rebooted.messages.lastThreadMessage(rebooted.active, last) && last.ts == 6,
                       "Legacy migration reboot lost its newest message");
        requireHistory(rebooted.messages.removeThread(rebooted.active, dirty) &&
                       rebooted.history.saveThreadsToStorage() && rebooted.history.saveMsgsToStorage(),
                       "Migrated legacy history deletion failed");
      }
      {
        Session empty(false, capacity);
        requireHistory(empty.history.loadHistoryFromStorage() && empty.messages.ring().count == 0 &&
                       empty.messages.findThreadByName("history-test", false) < 0,
                       "Empty committed segments resurrected the retained combined legacy file");
      }
    }
  }
  // Migration must not delete the only durable legacy file if the commit marker
  // cannot be written, even when every segment itself was written successfully.
  disk.enableMemory();
  disk.mkdir("/meshcomod");
  {
    auto legacy = disk.open("/meshcomod/ui_msgs_v1.bin", "w");
    UiMsgFileHeader header{};
    header.magic = k_ui_msgs_magic;
    header.version = k_ui_history_version;
    header.msg_rec_size = sizeof(UiHistoryMsg);
    header.ui_msg_count = 1;
    header.ui_msg_head = 0;
    UiHistoryMsg message{};
    strcpy(message.thread, "history-test");
    strcpy(message.text, "legacy");
    legacy.write(reinterpret_cast<const uint8_t *>(&header), sizeof header);
    legacy.write(reinterpret_cast<const uint8_t *>(&message), sizeof message);
  }
  {
    Session migration;
    migration.messages.findOrCreateThread("history-test", false, 10);
    disk.limitWrites(sizeof(UiSegHeader) + sizeof(UiSegMsg) + sizeof(UiHistoryHeader) +
                     MessageStore::MAX_UI_THREADS * sizeof(UiHistoryThread));
    requireHistory(migration.history.loadHistoryFromStorage(), "Legacy history was not loaded");
    requireHistory(!migration.history.status().ready && disk.exists("/meshcomod/ui_msgs_v1.bin"),
                   "Failed migration marker discarded legacy history");
    disk.allowWrites();
    requireHistory(migration.history.saveMsgsToStorage() && migration.history.status().ready,
                   "Migration retry failed");
    requireHistory(!disk.exists("/meshcomod/ui_msgs_v1.bin"),
                   "Successful migration retained obsolete message file");
  }
  // Exercise the same queued snapshot and completion path as FreeRTOS, with
  // an explicit desktop executor so failures and queued deletions are repeatable.
  disk.enableMemory();
  {
    Session asynchronous(true);
    for (int i = 0; i < 10; ++i)
      asynchronous.append(i);
    requireHistory(asynchronous.history.saveThreadsToStorage() && asynchronous.history.saveMsgsToStorage(),
                   "Async history setup failed");
    unsigned long deadline = millis();
    auto schedule = [&] {
      deadline += 60000;
      asynchronous.history.flushHistoryIfDue(deadline);
    };
    auto execute = [&] {
      requireHistory(asynchronous.history.pending(), "History did not queue its snapshot");
      requireHistory(!asynchronous.history.saveMsgsToStorage() && asynchronous.history.pending(),
                     "Synchronous save raced a queued worker snapshot");
      bool ran = false;
      std::thread worker([&] { ran = asynchronous.history.runPendingWrite(); });
      worker.join();
      requireHistory(ran && !asynchronous.history.active(), "History worker did not publish completion");
    };
    asynchronous.append(10);
    asynchronous.history.markMsgsDirty();
    schedule();
    {
      ui::platform::StorageTransition transition;
      requireHistory(transition.requested() && transition.ready(),
                     "History admission transition was unavailable");
      bool ran = true;
      std::thread denied([&] { ran = asynchronous.history.runPendingWrite(); });
      denied.join();
      requireHistory(!ran && asynchronous.history.pending(),
                     "Denied history write consumed its queued snapshot");
    }
    execute();
    requireHistory(ui::platform::storageAccess().readerCount() == 0,
                   "History worker retained a storage lease after completion");
    schedule();
    requireHistory(!asynchronous.history.active() && !asynchronous.history.messagesDirty(),
                   "Successful append remained pending");
    asynchronous.append(11);
    asynchronous.history.markMsgsDirty();
    schedule();
    disk.limitWrites(1);
    execute();
    requireHistory(ui::platform::storageAccess().readerCount() == 0,
                   "Failed history write retained a storage lease");
    disk.allowWrites();
    schedule();
    execute();
    schedule();
    requireHistory(!asynchronous.history.active() && !asynchronous.history.messagesDirty(),
                   "Repair flag was lost in asynchronous completion");
    int indexes[600];
    asynchronous.messages.getThreadMessageIndexes(asynchronous.active, indexes, 600, false);
    requireHistory(asynchronous.messages.deleteMessageBySlot(indexes[2], dirty),
                   "Async compaction setup failed");
    asynchronous.history.markMsgsDirty();
    schedule();
    requireHistory(asynchronous.messages.deleteMessageBySlot(indexes[3], dirty),
                   "Deletion during queued compaction failed");
    execute();
    schedule();
    execute();
    schedule();
    requireHistory(!asynchronous.history.active(), "Repeated compaction did not settle");
  }
  {
    Session reloaded;
    requireHistory(reloaded.history.loadHistoryFromStorage() && reloaded.count() == 10,
                   "Asynchronous append/repair/compaction lost or resurrected records");
  }

  // A queued snapshot can survive a storage transition because the worker
  // never claimed a StorageLease. A remount must cancel that descriptor and
  // let resync write the current ring to the replacement filesystem.
  disk.enableMemory();
  {
    Session remounted(true);
    for (int i = 0; i < 3; ++i)
      remounted.append(i);
    requireHistory(remounted.history.saveThreadsToStorage() && remounted.history.saveMsgsToStorage(),
                   "Remount resync history setup failed");
    remounted.append(3);
    remounted.history.markMsgsDirty();
    unsigned long deadline = millis();
    deadline += 60000;
    remounted.history.flushHistoryIfDue(deadline);
    requireHistory(remounted.history.pending(), "Remount scenario did not queue its old snapshot");
    {
      ui::platform::StorageTransition transition;
      requireHistory(transition.requested() && transition.enter(),
                     "Remount scenario could not close storage admission");
      disk.enableMemory();  // simulate formatting/replacing the filesystem
      remounted.history.flushHistorySoon();
      requireHistory(!remounted.history.pending(),
                     "Remount did not cancel the queued pre-format snapshot");
      requireHistory(!remounted.history.runPendingWrite(),
                     "Cancelled pre-format snapshot still ran");
    }
    deadline += 60000;
    remounted.history.flushHistoryIfDue(deadline);
    requireHistory(remounted.history.pending(), "Remount resync did not queue a fresh ring snapshot");
    bool ran = false;
    std::thread worker([&] { ran = remounted.history.runPendingWrite(); });
    worker.join();
    requireHistory(ran, "Remount resync snapshot did not execute");
    requireHistory(!disk.exists("/meshcomod/msgs/store.ok"),
                   "Resync committed before the UI accepted all segment completions");
    disk.limitWrites(sizeof(UiHistoryHeader) +
                     MessageStore::MAX_UI_THREADS * sizeof(UiHistoryThread) + 1);
    deadline += 60000;
    remounted.history.flushHistoryIfDue(deadline);
    requireHistory(remounted.history.messagesDirty() &&
                       !disk.exists("/meshcomod/msgs/store.ok"),
                   "Short resync marker write was accepted as a durable store");
    disk.allowWrites();
    deadline += 60000;
    remounted.history.flushHistoryIfDue(deadline);
    requireHistory(!remounted.history.pending() && !remounted.history.messagesDirty(),
                   "Remount resync did not settle");
  }
  {
    Session restored;
    requireHistory(restored.history.loadHistoryFromStorage() && restored.count() == 4,
                   "Remount resync did not preserve the current ring");
  }
  disk.enableMemory();
  {
    Session empty;
    requireHistory(empty.history.saveMsgsToStorage(), "Empty resync setup failed");
    disk.enableMemory();
    empty.history.flushHistorySoon();
    requireHistory(empty.history.saveMsgsToStorage() && disk.exists("/meshcomod/msgs/store.ok"),
                   "Synchronous empty resync did not publish a committed store");
  }
  {
    Session emptyReload;
    requireHistory(emptyReload.history.loadHistoryFromStorage() && emptyReload.count() == 0,
                   "Empty resync did not survive service recreation");
  }
  Serial.println("History regression: reload, segments, deletion, short write, migration rollback passed");
}

#include "services/FileOperations.h"
void runFileOperationsRegression() {
  fs::FS source, destination;
  source.enableMemory();
  destination.enableMemory();
  ui::FileOperations operations({malloc, free, nullptr});
  auto write = [&](const char *path, size_t count) {
    auto file = source.open(path, "w");
    for (size_t i = 0; i < count; ++i)
      file.write(static_cast<uint8_t>(i));
    file.close();
  };
  source.mkdir("/source");
  source.mkdir("/source/nested");
  write("/source/a", 5000);
  write("/source/nested/b", 17);
  write("/source/empty", 0);
  requireHistory(operations.copy(source, "/source", destination, "/copy", true), "Recursive copy failed");
  auto copied = destination.open("/copy/a");
  requireHistory(copied.size() == 5000, "Copy silently lost data");
  for (size_t i = 0; i < 5000; ++i)
    requireHistory(copied.read() == static_cast<uint8_t>(i), "Copy changed content");
  copied.close();
  requireHistory(destination.exists("/copy/nested/b") && destination.exists("/copy/empty"),
                 "Copy skipped nested or empty file");
  requireHistory(!operations.copy(source, "/source/a", source, "/source/a", false),
                 "Self-copy truncated its source");
  requireHistory(!operations.copy(source, "/source", source, "/source/inside", true),
                 "Copy allowed recursive self-containment");
  requireHistory(!operations.copy(source, "/source", source, "/source/../inside", true),
                 "Copy accepted an ambiguous path");
  destination.limitWrites(2200);
  requireHistory(!operations.copy(source, "/source/a", destination, "/short", false),
                 "Short write reported success");
  requireHistory(!destination.exists("/short") && source.open("/source/a").size() == 5000,
                 "Failed copy retained a partial file or changed source");
  destination.allowWrites();
  source.limitReads(2200);
  requireHistory(!operations.move(source, "/source/a", destination, "/failed-move", false),
                 "Short read reported a successful move");
  requireHistory(source.exists("/source/a") && !destination.exists("/failed-move"),
                 "Failed move deleted source or retained partial destination");
  source.allowReads();
  source.failRenames(true);
  requireHistory(operations.move(source, "/source/nested/b", source, "/moved", false),
                 "Move did not fall back after rename failure");
  requireHistory(!source.exists("/source/nested/b") && source.open("/moved").size() == 17,
                 "Move fallback lost its data");
  requireHistory(operations.remove(destination, "/copy"), "Recursive deletion failed");
  requireHistory(!destination.exists("/copy/a") && !destination.exists("/copy/nested/b") &&
                     !destination.exists("/copy"),
                 "Recursive deletion skipped entries");
  requireHistory(!operations.remove(source, "/"), "File operation accepted whole-volume deletion");
  requireHistory(source.exists("/source/a"), "Copy/deletion changed the source tree");
}
