// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MessageTypes.h"
#include <stddef.h>
namespace ui {
// UI-thread-owned message ring and thread index. No LVGL, filesystem, clock or
// radio dependency. Persistence supplies a callback for changed record sequences.
class MessageStore : public MessageTypes {
public:
  using DirtyRecord = void (*)(uint32_t seq);
  using Allocator = void *(*)(size_t bytes, bool prefer_external);
  MessageStore() = default;
  ~MessageStore();
  MessageStore(const MessageStore &) = delete;
  MessageStore &operator=(const MessageStore &) = delete;
  bool allocate(int capacity, Allocator allocate, void (*release)(void *));
  bool ready() const { return _ui_msgs && _ui_threads; }
  int findOrCreateThread(const char *name, bool channel, uint32_t now);
  bool markThreadRead(int idx);
  bool markAllThreadsRead();
  bool append(int thread, UIMessage message, bool unread, bool mention, uint16_t per_chat_cap,
              DirtyRecord evicted, DirtyRecord dirty);
  bool acknowledge(uint32_t ack, DirtyRecord dirty);
  int getUnreadTotal() const;
  void rebuildThreadHistoryFlags() const;
  bool threadHasMention(int idx) const;
  int getThreadCount(bool channel_mode, int out_indexes[], int max_out) const;
  int getCombinedInboxCount(int out_indexes[], int max_out) const;
  bool getThreadInfo(int idx, bool &channel, uint16_t &unread, uint32_t &ts, char *name,
                     size_t name_len) const;
  bool getMessageByIndex(int msg_idx, UIMessage &out) const;
  int getUnreadMentionCount() const;
  int getThreadMessageIndexes(int thread_idx, int out_indexes[], int max_out, bool newest_first) const;
  int findThreadByName(const char *name, bool channel) const;
  void sortThreadsByRecent(bool channel_mode, int out_indexes[], int &out_count) const;
  bool threadHasMessageHistory(int thread_idx) const;
  bool deleteMessageBySlot(int msg_idx, DirtyRecord dirty);
  int clearThreadHistory(int thread_idx, DirtyRecord dirty);
  int enforceHistoryCap(int thread_idx, uint16_t cap, DirtyRecord dirty);
  // Read-only views are valid until the next model mutation, on the UI thread.
  // An invalid thread index yields an unused, unmapped thread.
  const UIThread &thread(int index) const;
  struct RingView {
    const UIMessage *records;
    int capacity, count, head;
  };
  RingView ring() const { return {_ui_msgs, _ui_msg_cap, _ui_msg_count, _ui_msg_head}; }
  int capacity() const { return _ui_msg_cap; }
  uint32_t latestSequence() const { return _ui_seq_next ? _ui_seq_next - 1 : UINT32_MAX; }
  int threadAtOrdinal(int ordinal) const;
  int historyAt(const char *name, int back) const;
  bool lastThreadMessage(int index, UIMessage &out) const;
  bool removeThread(int index, DirtyRecord dirty, int *purged = nullptr);
  bool discardEmptyDirectThreads(int keep);
  void clearContactIndexes();
  bool bindContact(int index, int16_t contact, const uint8_t *pub = nullptr);
  bool bindChannel(int index, int16_t slot);
  bool renameThread(int index, const char *name, DirtyRecord dirty);
  enum class ContactChange { None, Binding, Renamed };
  // Reports durable key/name changes; merely refreshing a cached index does not
  // require a metadata write. No thread is allocated for an unused contact.
  ContactChange reconcileContact(int16_t contact, const char *name, const uint8_t pub[32], DirtyRecord dirty);
  bool setMessageTimestamp(int slot, uint32_t sequence, uint32_t timestamp, DirtyRecord dirty);
  // Streaming persistence import. Import runs synchronously on the UI thread;
  // the loader stages records while the ring is empty, then commits its shape.
  void resetMessages();
  bool restoreThread(int index, const UIThread &);
  bool restoreMessage(int slot, const UIMessage &);
  bool restoreRingState(int count, int head, uint32_t nextSequence = 1);
  bool finishChronologicalRestore(int total, uint32_t nextSequence);
  void resequenceMessages();

private:
  bool containsSlot(int slot) const;
  uint32_t _ui_seq_next = 1;
  int _ui_msg_count = 0, _ui_msg_head = 0, _ui_msg_cap = MAX_UI_MESSAGES;
  mutable uint16_t _thread_msgs[MAX_UI_THREADS] = {};
  mutable bool _thread_hist_dirty = true;
  UIMessage *_ui_msgs = nullptr;
  UIThread *_ui_threads = nullptr;
  void (*_release)(void *) = nullptr;
};
} // namespace ui
