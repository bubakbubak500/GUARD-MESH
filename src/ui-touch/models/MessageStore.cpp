#include "MessageStore.h"
#include <cstring>
namespace ui {
MessageStore::~MessageStore() {
  if (_release) {
    _release(_ui_msgs);
    _release(_ui_threads);
  }
}
bool MessageStore::allocate(int capacity, Allocator alloc, void (*release)(void *)) {
  if (_ui_msgs || _ui_threads)
    return _ui_msgs && _ui_threads;
  if (!alloc || !release || capacity <= 0)
    return false;
  _ui_msg_cap = capacity;
  size_t bytes = sizeof(UIMessage) * (size_t)_ui_msg_cap;
  _ui_msgs = static_cast<UIMessage *>(alloc(bytes, true));
  if (!_ui_msgs && _ui_msg_cap > MAX_UI_MESSAGES) {
    _ui_msg_cap = MAX_UI_MESSAGES;
    bytes = sizeof(UIMessage) * (size_t)_ui_msg_cap;
    _ui_msgs = static_cast<UIMessage *>(alloc(bytes, true));
  }
  if (!_ui_msgs)
    _ui_msgs = static_cast<UIMessage *>(alloc(bytes, false));
  const size_t thread_bytes = sizeof(UIThread) * MAX_UI_THREADS;
  _ui_threads = static_cast<UIThread *>(alloc(thread_bytes, true));
  if (!_ui_threads)
    _ui_threads = static_cast<UIThread *>(alloc(thread_bytes, false));
  if (!_ui_msgs || !_ui_threads) {
    release(_ui_msgs);
    release(_ui_threads);
    _ui_msgs = nullptr;
    _ui_threads = nullptr;
    return false;
  }
  _release = release;
  memset(_ui_msgs, 0, bytes);
  memset(_ui_threads, 0, thread_bytes);
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    _ui_threads[i].mesh_contact_idx = -1;
    _ui_threads[i].mesh_channel_slot = -1;
  }
  return true;
}
void MessageStore::rebuildThreadHistoryFlags() const {
  if (!_ui_threads || !_ui_msgs)
    return;
  for (int i = 0; i < MAX_UI_THREADS; ++i)
    _thread_msgs[i] = 0;
  for (int i = 0; i < _ui_msg_count; ++i) {
    const int idx = (_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap;
    const UIMessage &m = _ui_msgs[idx];
    if (!m.thread[0])
      continue; // blanked by a clear / cap trim
    for (int t = 0; t < MAX_UI_THREADS; ++t) {
      if (!_ui_threads[t].used)
        continue;
      if (m.channel == _ui_threads[t].channel &&
          strncmp(m.thread, _ui_threads[t].name, MAX_THREAD_NAME) == 0) {
        if (_thread_msgs[t] < 0xFFFF)
          ++_thread_msgs[t];
        break;
      }
    }
  }
  _thread_hist_dirty = false;
}

bool MessageStore::threadHasMention(int idx) const {
  if (!_ui_threads || !_ui_msgs)
    return false;
  return idx >= 0 && idx < MAX_UI_THREADS && _ui_threads[idx].used && _ui_threads[idx].has_mention;
}

int MessageStore::getThreadCount(bool channel_mode, int out_indexes[], int max_out) const {
  if (!_ui_threads || !out_indexes || max_out <= 0)
    return 0;
  int scratch[MAX_UI_THREADS];
  int n = 0;
  sortThreadsByRecent(channel_mode, scratch, n);
  int copy_n = (n < max_out) ? n : max_out;
  for (int i = 0; i < copy_n; ++i)
    out_indexes[i] = scratch[i];
  return copy_n;
}

int MessageStore::getCombinedInboxCount(int out_indexes[], int max_out) const {
  if (!_ui_threads || !out_indexes || max_out <= 0)
    return 0;
  int scratch[MAX_UI_THREADS];
  int n = 0;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used)
      continue;
    if (!_ui_threads[i].channel && !threadHasMessageHistory(i))
      continue;
    scratch[n++] = i;
  }
  for (int a = 0; a < n; ++a) {
    for (int b = a + 1; b < n; ++b) {
      if (_ui_threads[scratch[b]].last_ts > _ui_threads[scratch[a]].last_ts) {
        int t = scratch[a];
        scratch[a] = scratch[b];
        scratch[b] = t;
      }
    }
  }
  const int copy_n = (n < max_out) ? n : max_out;
  for (int i = 0; i < copy_n; ++i)
    out_indexes[i] = scratch[i];
  return copy_n;
}

bool MessageStore::getThreadInfo(int idx, bool &channel, uint16_t &unread, uint32_t &ts, char *name,
                                 size_t name_len) const {
  if (!_ui_threads || !_ui_msgs)
    return false;
  if (idx < 0 || idx >= MAX_UI_THREADS || !_ui_threads[idx].used)
    return false;
  channel = _ui_threads[idx].channel;
  unread = _ui_threads[idx].unread;
  ts = _ui_threads[idx].last_ts;
  if (name && name_len > 0) {
    strncpy(name, _ui_threads[idx].name, name_len - 1);
    name[name_len - 1] = '\0';
  }
  return true;
}

bool MessageStore::getMessageByIndex(int msg_idx, UIMessage &out) const {
  if (!containsSlot(msg_idx))
    return false;
  out = _ui_msgs[msg_idx];
  return true;
}

int MessageStore::getUnreadMentionCount() const {
  if (!_ui_threads)
    return 0;
  int c = 0;
  for (int i = 0; i < MAX_UI_THREADS; i++)
    if (_ui_threads[i].used && _ui_threads[i].has_mention)
      c++;
  return c;
}

int MessageStore::getThreadMessageIndexes(int thread_idx, int out_indexes[], int max_out,
                                          bool newest_first) const {
  if (!_ui_threads || !out_indexes || max_out <= 0)
    return 0;
  if (thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used)
    return 0;
  int n = 0;
  for (int i = 0; i < _ui_msg_count && n < max_out; ++i) {
    int idx = (_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap;
    const UIMessage &m = _ui_msgs[idx];
    if (strncmp(m.thread, _ui_threads[thread_idx].name, MAX_THREAD_NAME) == 0 &&
        m.channel == _ui_threads[thread_idx].channel)
      out_indexes[n++] = idx;
  }
  if (!newest_first) {
    for (int i = 0; i < n / 2; ++i) {
      int tmp = out_indexes[i];
      out_indexes[i] = out_indexes[n - 1 - i];
      out_indexes[n - 1 - i] = tmp;
    }
  }
  return n;
}

int MessageStore::findThreadByName(const char *name, bool channel) const {
  if (!_ui_threads || !name)
    return -1;
  if (!name || !name[0])
    return -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel &&
        strncmp(_ui_threads[i].name, name, MAX_THREAD_NAME) == 0)
      return i;
  }
  return -1;
}

void MessageStore::sortThreadsByRecent(bool channel_mode, int out_indexes[], int &out_count) const {
  out_count = 0;
  if (!_ui_threads || !out_indexes)
    return;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel_mode)
      out_indexes[out_count++] = i;
  }
  for (int i = 0; i < out_count; ++i) {
    for (int j = i + 1; j < out_count; ++j) {
      if (_ui_threads[out_indexes[j]].last_ts > _ui_threads[out_indexes[i]].last_ts) {
        int tmp = out_indexes[i];
        out_indexes[i] = out_indexes[j];
        out_indexes[j] = tmp;
      }
    }
  }
}

bool MessageStore::threadHasMessageHistory(int thread_idx) const {
  if (!_ui_threads || !_ui_msgs)
    return false;
  if (thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used)
    return false;
  if (_thread_hist_dirty)
    rebuildThreadHistoryFlags();
  return _thread_msgs[thread_idx] > 0;
}

bool MessageStore::deleteMessageBySlot(int msg_idx, DirtyRecord dirty) {
  if (!_ui_msgs || !_ui_threads)
    return 0;
  if (msg_idx < 0 || msg_idx >= _ui_msg_cap)
    return false;
  UIMessage &m = _ui_msgs[msg_idx];
  if (!m.thread[0])
    return false; // free or already tombstoned
  if (isUnreadMessage(msg_idx)) {
    const int thread = findThreadByName(m.thread, m.channel);
    if (--_ui_threads[thread].unread == 0) _ui_threads[thread].has_mention = false;
  }
  m.thread[0] = '\0';
  m.text[0] = '\0';
  m.sender[0] = '\0';
  if (dirty)
    dirty(m.seq); // the owning segment needs a compaction pass
  _thread_hist_dirty = true;
  return true;
}

int MessageStore::clearThreadHistory(int thread_idx, DirtyRecord dirty) {
  if (!_ui_msgs || !_ui_threads)
    return 0;
  if (thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used)
    return 0;
  int cleared = 0;
  for (int i = 0; i < _ui_msg_count; ++i) {
    const int idx = (_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap;
    UIMessage &m = _ui_msgs[idx];
    if (m.thread[0] && strncmp(m.thread, _ui_threads[thread_idx].name, MAX_THREAD_NAME) == 0 &&
        m.channel == _ui_threads[thread_idx].channel) {
      m.thread[0] = '\0';
      m.text[0] = '\0';
      m.sender[0] = '\0';
      if (dirty)
        dirty(m.seq);
      ++cleared;
    }
  }
  _ui_threads[thread_idx].unread = 0;
  _ui_threads[thread_idx].has_mention = false;
  _thread_msgs[thread_idx] = 0; // emptied, and no other thread was touched
  _thread_hist_dirty = true;
  return cleared;
}

int MessageStore::enforceHistoryCap(int thread_idx, uint16_t cap, DirtyRecord dirty) {
  if (!_ui_msgs || !_ui_threads)
    return 0;
  if (!cap || thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used)
    return 0;
  if (_thread_hist_dirty)
    rebuildThreadHistoryFlags();
  if (_thread_msgs[thread_idx] <= cap)
    return 0;
  const bool ch = _ui_threads[thread_idx].channel;
  const char *nm = _ui_threads[thread_idx].name;
  int trimmed = 0;
  for (int i = _ui_msg_count - 1; i >= 0 && _thread_msgs[thread_idx] > cap; --i) {
    const int idx = (_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap; // i = oldest .. 0 = newest
    UIMessage &m = _ui_msgs[idx];
    if (!m.thread[0] || m.channel != ch)
      continue;
    if (strncmp(m.thread, nm, MAX_THREAD_NAME) != 0)
      continue;
    m.thread[0] = '\0';
    m.text[0] = '\0';
    m.sender[0] = '\0';
    if (dirty)
      dirty(m.seq);
    --_thread_msgs[thread_idx];
    ++trimmed;
  }
  if (trimmed) {
    unsigned incoming = 0;
    for (int i = 0; i < _ui_msg_count; ++i) {
      const auto& m = _ui_msgs[(_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap];
      if (!m.outgoing && m.channel == ch && !strncmp(m.thread, nm, MAX_THREAD_NAME)) ++incoming;
    }
    if (_ui_threads[thread_idx].unread > incoming) _ui_threads[thread_idx].unread = incoming;
    if (!_ui_threads[thread_idx].unread) _ui_threads[thread_idx].has_mention = false;
  }
  return trimmed;
}

int MessageStore::getUnreadTotal() const {
  if (!_ui_threads)
    return 0;
  int total = 0;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used)
      continue;
    if (!_ui_threads[i].channel && !threadHasMessageHistory(i))
      continue;
    total += _ui_threads[i].unread;
  }
  return total;
}
int MessageStore::findOrCreateThread(const char *name, bool channel, uint32_t now) {
  if (!name || !name[0])
    return -1;
  // _ui_threads is allocated further down begin(), which console mode returns
  // before, so it is null there. An incoming message reached here and
  // dereferenced it: the device panicked, and since it rebooted straight back
  // into console mode it did so every time a message arrived. Guard rather than
  // allocate: in console mode nothing reads the table.
  if (!_ui_threads)
    return -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel &&
        strncmp(_ui_threads[i].name, name, MAX_THREAD_NAME) == 0)
      return i;
  }
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used)
      continue;
    _ui_threads[i].used = true;
    _ui_threads[i].channel = channel;
    _ui_threads[i].mesh_contact_idx = -1;
    memset(_ui_threads[i].mesh_contact_pub, 0, sizeof(_ui_threads[i].mesh_contact_pub));
    memset(_ui_threads[i].mesh_contact_key6, 0, sizeof(_ui_threads[i].mesh_contact_key6));
    _ui_threads[i].mesh_channel_slot = -1;
    _ui_threads[i].unread = 0;
    _ui_threads[i].has_mention = false;
    _ui_threads[i].last_ts = now;
    strncpy(_ui_threads[i].name, name, MAX_THREAD_NAME);
    _ui_threads[i].name[MAX_THREAD_NAME] = '\0';
    _thread_hist_dirty = true;
    return i;
  }
  return -1;
}
bool MessageStore::markThreadRead(int idx) {
  if (!_ui_threads)
    return false;
  if (idx < 0 || idx >= MAX_UI_THREADS || !_ui_threads[idx].used)
    return false;
  if (_ui_threads[idx].unread == 0 && !_ui_threads[idx].has_mention)
    return false;
  _ui_threads[idx].unread = 0;
  _ui_threads[idx].has_mention = false;
  return true;
}
bool MessageStore::markAllThreadsRead() {
  if (!_ui_threads)
    return false;
  bool any = false;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used)
      continue;
    if (_ui_threads[i].unread || _ui_threads[i].has_mention) {
      _ui_threads[i].unread = 0;
      _ui_threads[i].has_mention = false;
      any = true;
    }
  }
  return any;
}

bool MessageStore::append(int thread, UIMessage message, bool unread, bool mention, uint16_t per_chat_cap,
                          DirtyRecord evicted, DirtyRecord dirty) {
  if (!_ui_msgs || !_ui_threads || !_ui_seq_next || thread < 0 || thread >= MAX_UI_THREADS ||
      !_ui_threads[thread].used)
    return false;
  if (_ui_msg_count == _ui_msg_cap) {
    if (evicted)
      evicted(_ui_msgs[_ui_msg_head].seq);
    _thread_hist_dirty = true;
  }
  message.seq = _ui_seq_next++;
  message.channel = _ui_threads[thread].channel;
  strncpy(message.thread, _ui_threads[thread].name, MAX_THREAD_NAME);
  message.thread[MAX_THREAD_NAME] = '\0';
  message.sender[MAX_SENDER_NAME] = '\0';
  message.text[MAX_MSG_TEXT] = '\0';
  if (message.in_path_n > MAX_UI_PATH)
    message.in_path_n = MAX_UI_PATH;
  _ui_msgs[_ui_msg_head] = message;
  if (!_thread_hist_dirty && _thread_msgs[thread] < 0xFFFF)
    ++_thread_msgs[thread];
  if (_ui_msg_count < _ui_msg_cap)
    ++_ui_msg_count;
  _ui_msg_head = (_ui_msg_head + 1) % _ui_msg_cap;
  _ui_threads[thread].last_ts = message.ts;
  if (unread && _ui_threads[thread].unread < 0xFFFF)
    ++_ui_threads[thread].unread;
  if (mention)
    _ui_threads[thread].has_mention = true;
  enforceHistoryCap(thread, per_chat_cap, dirty);
  return true;
}
bool MessageStore::acknowledge(uint32_t ack, DirtyRecord dirty) {
  if (!ack || !_ui_msgs)
    return false;
  bool changed = false;
  for (int i = 0; i < _ui_msg_count; ++i) {
    UIMessage &m = _ui_msgs[(_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap];
    if (!m.thread[0] || !m.outgoing || m.channel || m.ack_hash != ack || m.deliv_state == DELIV_DELIVERED)
      continue;
    m.deliv_state = DELIV_DELIVERED;
    if (dirty)
      dirty(m.seq);
    changed = true;
  }
  return changed;
}

const MessageStore::UIThread &MessageStore::thread(int index) const {
  static const UIThread empty = [] {
    UIThread value{};
    value.mesh_contact_idx = -1;
    value.mesh_channel_slot = -1;
    return value;
  }();
  return _ui_threads && index >= 0 && index < MAX_UI_THREADS ? _ui_threads[index] : empty;
}
int MessageStore::threadAtOrdinal(int ordinal) const {
  if (ordinal < 0)
    return -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i)
    if (thread(i).used && ordinal-- == 0)
      return i;
  return -1;
}
int MessageStore::historyAt(const char *name, int back) const {
  if (!ready() || !name || !*name || back < 0)
    return -1;
  for (int i = 0; i < _ui_msg_count; ++i) {
    const int slot = (_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap;
    if (!strncmp(_ui_msgs[slot].thread, name, MAX_THREAD_NAME) && back-- == 0)
      return slot;
  }
  return -1;
}
bool MessageStore::lastThreadMessage(int index, UIMessage &out) const {
  const auto &value = thread(index);
  if (!ready() || !value.used)
    return false;
  // Arrival order, not wall-clock order: clocks may move backwards after reboot.
  for (int i = 0; i < _ui_msg_count; ++i) {
    const auto &message = _ui_msgs[(_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap];
    if (message.text[0] && message.channel == value.channel &&
        !strncmp(message.thread, value.name, MAX_THREAD_NAME)) {
      out = message;
      return true;
    }
  }
  return false;
}
int MessageStore::newestUnread(int slots[], int threads[], int capacity) const {
  if (!ready() || !slots || !threads || capacity <= 0) return 0;
  uint16_t remaining[MAX_UI_THREADS]{};
  unsigned total = 0;
  for (int i=0; i<MAX_UI_THREADS; ++i) {
    remaining[i] = _ui_threads[i].used ? _ui_threads[i].unread : 0;
    total += remaining[i];
  }
  int count = 0;
  for (int i=0; total && i<_ui_msg_count && count<capacity; ++i) {
    const int slot = (_ui_msg_head-1-i+_ui_msg_cap)%_ui_msg_cap;
    const auto& m = _ui_msgs[slot];
    if (!m.thread[0] || m.outgoing) continue;
    const int thread = findThreadByName(m.thread,m.channel);
    if (thread<0 || !remaining[thread]) continue;
    --remaining[thread]; --total;
    slots[count] = slot; threads[count++] = thread;
  }
  return count;
}
bool MessageStore::isUnreadMessage(int slot) const {
  if (!ready() || !containsSlot(slot)) return false;
  const auto& target = _ui_msgs[slot];
  if (!target.thread[0] || target.outgoing) return false;
  const int thread = findThreadByName(target.thread,target.channel);
  if (thread<0) return false;
  unsigned remaining = _ui_threads[thread].unread;
  for (int i=0; remaining && i<_ui_msg_count; ++i) {
    const int current = (_ui_msg_head-1-i+_ui_msg_cap)%_ui_msg_cap;
    const auto& m = _ui_msgs[current];
    if (m.outgoing || m.channel!=target.channel || strncmp(m.thread,target.thread,MAX_THREAD_NAME)) continue;
    if (current==slot) return true;
    --remaining;
  }
  return false;
}
bool MessageStore::removeThread(int index, DirtyRecord dirty, int *purged) {
  if (purged)
    *purged = 0;
  if (!ready() || !thread(index).used)
    return false;
  const UIThread value = _ui_threads[index];
  for (int i = 0; i < _ui_msg_cap; ++i) {
    auto &message = _ui_msgs[i];
    if (message.channel != value.channel || strncmp(message.thread, value.name, MAX_THREAD_NAME))
      continue;
    message.thread[0] = message.sender[0] = message.text[0] = 0;
    if (dirty)
      dirty(message.seq);
    if (purged)
      ++*purged;
  }
  _ui_threads[index] = UIThread{};
  _ui_threads[index].mesh_contact_idx = _ui_threads[index].mesh_channel_slot = -1;
  _thread_hist_dirty = true;
  return true;
}
bool MessageStore::discardEmptyDirectThreads(int keep) {
  if (!ready())
    return false;
  bool changed = false;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (i == keep || !_ui_threads[i].used || _ui_threads[i].channel || threadHasMessageHistory(i))
      continue;
    _ui_threads[i] = UIThread{};
    _ui_threads[i].mesh_contact_idx = _ui_threads[i].mesh_channel_slot = -1;
    changed = true;
  }
  if (changed) _thread_hist_dirty = true;
  return changed;
}
void MessageStore::clearContactIndexes() {
  if (!ready())
    return;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    auto &value = _ui_threads[i];
    if (!value.used || value.channel)
      continue;
    uint8_t any = 0;
    for (auto byte : value.mesh_contact_pub)
      any |= byte;
    for (auto byte : value.mesh_contact_key6)
      any |= byte;
    if (any)
      value.mesh_contact_idx = -1;
  }
}
bool MessageStore::bindContact(int index, int16_t contact, const uint8_t *pub) {
  if (!thread(index).used || thread(index).channel)
    return false;
  auto &value = _ui_threads[index];
  value.mesh_contact_idx = contact;
  if (pub) {
    memcpy(value.mesh_contact_pub, pub, sizeof value.mesh_contact_pub);
    memcpy(value.mesh_contact_key6, pub, sizeof value.mesh_contact_key6);
  }
  return true;
}
bool MessageStore::bindChannel(int index, int16_t slot) {
  if (!thread(index).used || !thread(index).channel)
    return false;
  _ui_threads[index].mesh_channel_slot = slot;
  return true;
}
bool MessageStore::renameThread(int index, const char *name, DirtyRecord dirty) {
  if (!ready() || !thread(index).used || !name || !*name)
    return false;
  char replacement[MAX_THREAD_NAME + 1];
  strncpy(replacement, name, MAX_THREAD_NAME);
  replacement[MAX_THREAD_NAME] = 0;
  auto &value = _ui_threads[index];
  if (!strcmp(value.name, replacement))
    return false;
  const int other = findThreadByName(replacement, value.channel);
  if (other >= 0 && other != index)
    return false; // never merge two identities by name
  for (int i = 0; i < _ui_msg_count; ++i) {
    auto &message = _ui_msgs[(_ui_msg_head - 1 - i + _ui_msg_cap) % _ui_msg_cap];
    if (message.channel != value.channel || strncmp(message.thread, value.name, MAX_THREAD_NAME))
      continue;
    memcpy(message.thread, replacement, sizeof replacement);
    if (dirty)
      dirty(message.seq);
  }
  memcpy(value.name, replacement, sizeof replacement);
  _thread_hist_dirty = true;
  return true;
}
bool MessageStore::setMessageTimestamp(int slot, uint32_t sequence, uint32_t timestamp, DirtyRecord dirty) {
  if (!containsSlot(slot) || !_ui_msgs[slot].thread[0] || !sequence || _ui_msgs[slot].seq != sequence)
    return false;
  if (_ui_msgs[slot].ts == timestamp)
    return true;
  _ui_msgs[slot].ts = timestamp;
  if (dirty)
    dirty(sequence);
  return true;
}
void MessageStore::resetMessages() {
  if (_ui_msgs)
    memset(_ui_msgs, 0, sizeof(UIMessage) * _ui_msg_cap);
  _ui_msg_count = _ui_msg_head = 0;
  _thread_hist_dirty = true;
}
bool MessageStore::restoreThread(int index, const UIThread &value) {
  if (!ready() || index < 0 || index >= MAX_UI_THREADS)
    return false;
  _ui_threads[index] = value;
  _ui_threads[index].name[MAX_THREAD_NAME] = 0;
  _thread_hist_dirty = true;
  return true;
}
bool MessageStore::restoreMessage(int slot, const UIMessage &value) {
  if (!ready() || _ui_msg_count != 0 || slot < 0 || slot >= _ui_msg_cap)
    return false;
  auto &message = _ui_msgs[slot];
  message = value;
  message.thread[MAX_THREAD_NAME] = message.sender[MAX_SENDER_NAME] = message.text[MAX_MSG_TEXT] = 0;
  if (message.in_path_n > MAX_UI_PATH)
    message.in_path_n = MAX_UI_PATH;
  _thread_hist_dirty = true;
  return true;
}
bool MessageStore::restoreRingState(int count, int head, uint32_t nextSequence) {
  if (!ready() || count < 0 || count > _ui_msg_cap || head < 0 || head >= _ui_msg_cap)
    return false;
  _ui_msg_count = count;
  _ui_msg_head = head;
  _ui_seq_next = nextSequence;
  _thread_hist_dirty = true;
  return true;
}
bool MessageStore::finishChronologicalRestore(int total, uint32_t nextSequence) {
  if (!ready() || total < 0)
    return false;
  const int kept = total > _ui_msg_cap ? _ui_msg_cap : total;
  const int rotate = total > _ui_msg_cap ? total % _ui_msg_cap : 0;
  if (rotate) {
    const auto reverse = [this](int first, int last) {
      while (first < last) {
        UIMessage value = _ui_msgs[first];
        _ui_msgs[first++] = _ui_msgs[last];
        _ui_msgs[last--] = value;
      }
    };
    reverse(0, rotate - 1);
    reverse(rotate, _ui_msg_cap - 1);
    reverse(0, _ui_msg_cap - 1);
  }
  return restoreRingState(kept, kept % _ui_msg_cap, nextSequence);
}
void MessageStore::resequenceMessages() {
  if (!ready())
    return;
  for (int i = 0; i < _ui_msg_count; ++i)
    _ui_msgs[(_ui_msg_head - _ui_msg_count + i + _ui_msg_cap) % _ui_msg_cap].seq = uint32_t(i + 1);
  _ui_seq_next = uint32_t(_ui_msg_count) + 1;
}

bool MessageStore::containsSlot(int slot) const {
  if (!ready() || slot < 0 || slot >= _ui_msg_cap)
    return false;
  const int oldest = (_ui_msg_head - _ui_msg_count + _ui_msg_cap) % _ui_msg_cap;
  return (slot - oldest + _ui_msg_cap) % _ui_msg_cap < _ui_msg_count;
}
MessageStore::ContactChange MessageStore::reconcileContact(int16_t contact, const char *name,
                                                           const uint8_t pub[32], DirtyRecord dirty) {
  if (!ready() || !name || !*name || !pub)
    return ContactChange::None;
  const auto hasKey = [](const uint8_t *key, size_t size) {
    uint8_t any = 0;
    for (size_t i = 0; i < size; ++i)
      any |= key[i];
    return any != 0;
  };
  int index = -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    const auto &value = thread(i);
    if (!value.used || value.channel)
      continue;
    const bool full = hasKey(value.mesh_contact_pub, 32);
    if ((full && !memcmp(value.mesh_contact_pub, pub, 32)) ||
        (!full && hasKey(value.mesh_contact_key6, 6) && !memcmp(value.mesh_contact_key6, pub, 6))) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    index = findThreadByName(name, false);
    if (index < 0)
      return ContactChange::None; // do not preallocate a DM for every discovered peer
    const auto &value = thread(index);
    if (hasKey(value.mesh_contact_pub, 32)) {
      if (memcmp(value.mesh_contact_pub, pub, 32))
        return ContactChange::None;
    } else if (hasKey(value.mesh_contact_key6, 6) && memcmp(value.mesh_contact_key6, pub, 6))
      return ContactChange::None;
  }
  const auto &current = thread(index);
  const bool bindingChanged =
      memcmp(current.mesh_contact_pub, pub, 32) || memcmp(current.mesh_contact_key6, pub, 6);
  bindContact(index, contact, pub);
  if (renameThread(index, name, dirty))
    return ContactChange::Renamed;
  return bindingChanged ? ContactChange::Binding : ContactChange::None;
}

} // namespace ui
