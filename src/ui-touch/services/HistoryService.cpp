// SPDX-License-Identifier: GPL-3.0-or-later
#include "HistoryService.h"
#include "HistoryCodec.h"
#include "HistoryFileStore.h"
#include "../platform/StorageAccess.h"
#include "../platform/UiDevice.h"
namespace ui { namespace history {
HistoryService::~HistoryService() {
  // Production owns this service for the whole boot. Tests may recreate it.
  // Join any claimed write before freeing its snapshot or task stack.
  _worker.cancel();
#if defined(ESP32)
  while (_worker.running()) delay(10);
  if (s_hist_flush_task) vTaskDelete(s_hist_flush_task);
  if (s_hist_flush_stack) heap_caps_free(s_hist_flush_stack);
#endif
  if (s_segjob_buf) heap_caps_free(s_segjob_buf);
  if (s_segsync_buf) heap_caps_free(s_segsync_buf);
}
void HistoryService::configure(MessageStore& messages, int& counter, Host host) {
  _messages = &messages; _msgcount = &counter; _host = host;
}
void HistoryService::restoreSelection(int index, bool channel) {
  Selection value(index, channel);
  if (index < 0 || index >= MAX_UI_THREADS || !_messages->thread(index).used ||
      _messages->thread(index).channel != channel) value = Selection();
  if (_host.restoreSelection) _host.restoreSelection(_host.selectionContext, value);
}
HistoryService::Status HistoryService::status() const {
  return {s_msgs_write_ok_ms, s_msgs_write_ok_epoch, s_msgs_write_fail_epoch,
          s_seg_total_bytes, s_msgs_write_fails, s_seg_count, s_seg_store_ready};
}
File HistoryService::uiDataOpen(const char* name, const char* mode) {
  if (!uiDataFsReady()) return File();
  File f = FileStore<fs::FS>(*_host.filesystem(), _host.root()).open(name, mode);
  if (!f && mode && mode[0] == 'w' && uiDataFsIsSdCard() && _host.ioFailure) _host.ioFailure();
  return f;
}
void HistoryService::uiDataRemove(const char* name) {
  if (uiDataFsReady()) FileStore<fs::FS>(*_host.filesystem(), _host.root()).remove(name);
}
bool HistoryService::uiDataReplaceFile(const char* name, const char* temporary) {
  return uiDataFsReady() && FileStore<fs::FS>(*_host.filesystem(), _host.root()).replace(name, temporary);
}
static bool readHistoryRec(File& file, void* out, size_t size, size_t diskSize) {
  return readRecord(file, out, size, diskSize);
}
// Human-readable diagnosis of the last chat-store write failure — shown on the
// About panel and in the failure alerts, so a tester doesn't need an errno
// table. Diagnostic vocabulary stays English on purpose (it's what ends up in
// bug reports verbatim).
void HistoryService::chatSaveFailText(char* out, size_t cap) {
  const char* stage;
  switch ((char)s_msgs_write_stage) {
    case 'o': stage = "open";        break;
    case 'h': stage = "header";      break;
    case 'b': stage = "write";       break;
    case 'r': stage = "rename";      break;
    case 'a': stage = "append";      break;   // segmented store: active-segment append
    case 'c': stage = "compact";     break;   // segmented store: one-segment rewrite
    case 'd': stage = "mkdir";       break;   // segmented store: data-dir create
    case 's': stage = "scan";        break;   // segmented store: segment discovery
    case 'm': stage = "commit";      break;   // segmented store: migration commit marker
    default:  stage = "save";        break;
  }
  const int e = s_msgs_write_errno;
  const char* why;
  switch (e) {
    case ENFILE:
    case EMFILE: why = "too many open files"; break;
    case ENOSPC: why = "card full";           break;
    case EIO:    why = "card I/O error";      break;
    case ENOENT: why = "folder missing";      break;
    case EROFS:  why = "write-protected";     break;
    case EACCES: why = "access denied";       break;
    case 0:      why = "no errno";            break;
    default:     why = strerror(e);           break;   // newlib carries the full table
  }
  snprintf(out, cap, "%s failed: %s (e%d)", stage, why, e);
}

void HistoryService::markThreadsDirty(unsigned long delay_ms) {
  const unsigned long deadline = millis() + delay_ms;
  if (!_threads_dirty || deadline < _next_threads_flush_ms)
    _next_threads_flush_ms = deadline;
  _threads_dirty = true;
}

void HistoryService::markMsgsDirty(unsigned long delay_ms) {
  // Segmented store: a flush is now a small APPEND to the active segment
  // (~240 B per message, off-thread), so the old 10/30 s whole-ring-rewrite
  // clamps are gone — the hard-cut loss window shrinks to this coalesce.
  // SPIFFS keeps a slightly higher floor purely as flash-wear pacing.
  if (uiDataFsIsSdCard()) { if (delay_ms < 2000) delay_ms = 2000; }
  else                    { if (delay_ms < 5000) delay_ms = 5000; }
  const unsigned long deadline = millis() + delay_ms;
  if (!_msgs_dirty || deadline < _next_msgs_flush_ms)
    _next_msgs_flush_ms = deadline;
  _msgs_dirty = true;
  markThreadsDirty(1500);  // thread state (last_ts, unread) changed too — coalesce a message burst into one write
}

// Arm an immediate flush of both history files WITHOUT the min-delay clamp and
// WITHOUT blocking this task: the actual writes happen on the hist_flush
// worker (or the sync fallback inside flushHistoryIfDue). Called only from the
// SD remount paths — the card may be a DIFFERENT one now, so the segment
// table is rebuilt from the ring (s_seg_resync) and the full history re-lands
// one bounded segment at a time.
void HistoryService::flushHistorySoon() {
  // A remount can leave a descriptor queued while storage admission is
  // closed. Drop that unclaimed snapshot so the first post-remount write is
  // built from the ring after the resync table has been rebuilt. A claimed
  // worker is left alone; it owns its descriptor until completion.
  if (_worker.cancel()) {
    s_segjob_kind    = SEGJOB_NONE;
    s_segjob_redirty = false;
  }
  s_seg_resync           = true;
  _msgs_dirty            = true;
  _next_msgs_flush_ms    = millis();
  _threads_dirty         = true;
  _next_threads_flush_ms = millis();
}

void HistoryService::flushHistoryIfDue(unsigned long now) {
  if (!_messages || !_messages->ready()) return;
  // Observe a finished worker job FIRST — and strictly BEFORE the failure
  // branch below resets s_hist_flush_ok, or a FAILED job would read as ok
  // here and get committed, advancing the durability watermark over records
  // that never reached disk (silent permanent loss).
  if (s_segjob_kind != SEGJOB_NONE && !_worker.active()) {
    if (s_hist_flush_ok) {
      SegJob done{};
      done.kind      = s_segjob_kind;
      done.first_seq = s_segjob_first_seq;
      done.last_seq  = s_segjob_last_seq;
      done.create    = s_segjob_create;
      done.repair    = s_segjob_repair;
      done.n         = s_segjob_n;
      const uint32_t pre_wm = s_seg_flushed_seq;
      segCommitJob(done);
      // Deletes that raced the in-flight APPEND: segMarkSeqDirty skipped them
      // while their seq was above the watermark ("never reaches disk") — but
      // this job just put them on disk. Give their segments the compaction
      // mark now.
      if (s_seg_flushed_seq > pre_wm) {
        for (int i = 0; i < _messages->ring().count; ++i) {
          const int slot = (_messages->ring().head - _messages->ring().count + i + _messages->capacity()) % _messages->capacity();
          const UIMessage& m = _messages->ring().records[slot];
          if (!m.thread[0] && m.seq > pre_wm && m.seq <= s_seg_flushed_seq) {
            segMarkSeqDirty(m.seq);
            _msgs_dirty = true;   // schedule the follow-up compaction
          }
        }
      }
    }
    s_segjob_kind = SEGJOB_NONE;   // failed job: data stays pending below the watermark
    s_segjob_redirty = false;
  }
  // A worker write failed (storage hiccup): re-arm and try again. Surface
  // repeated failures — a persistently failing ring write was previously
  // 100% silent and read as "messages from the last N minutes vanished" on
  // the next reboot (the small threads file kept writing, so the thread
  // list still showed fresh last-message times).
  if (!s_hist_flush_ok) {
    s_hist_flush_ok = true;
    // Back off a hopelessly failing store: each retry against a card that
    // times out on large writes costs seconds of bus stalls (felt as UI
    // freezes through the FatFs volume lock). After 5 straight failures
    // retry every 5 min instead of every 30 s — the RAM ring keeps
    // everything meanwhile, and a successful write resets the counter.
    markMsgsDirty(s_msgs_write_fails >= 5 ? 300000 : 5000);
    if (s_msgs_write_fails >= 3 && (s_msgs_write_fails % 3) == 0) {
      char why[64]; chatSaveFailText(why, sizeof why);
      char msg[128];
      snprintf(msg, sizeof msg, "%s\n%s", TR("Chat history is NOT saving"), why);
      if (_host.alert) _host.alert(msg, 3200);
    }
  }
  // Thread metadata (~4 KB) flushes on a short delay; the message ring
  // (scales with MAX_UI_MESSAGES) flushes lazily to reduce flash write pressure.
  if (_threads_dirty && now >= _next_threads_flush_ms) {
    if (_worker.active()) {
      // The worker owns or may claim a write on the same filesystem; SPIFFS serializes
      // internally, so writing now would block the loop behind its GC. Defer.
      _next_threads_flush_ms = now + 1000;
    } else if (saveThreadsToStorage()) _threads_dirty = false;
    else _next_threads_flush_ms = now + 2000;
  }
  if (_msgs_dirty && now >= _next_msgs_flush_ms) {
    if (_worker.active()) {
      // A request is armed but the flush task may not be up yet — kick it so a
      // pending flush can't sit armed forever while messages live only in RAM.
      ensureHistFlushTaskRunning();
      _next_msgs_flush_ms = now + 1000;   // one job in flight; new messages ride the next one
      return;
    }
#if defined(ESP32) || defined(GUARD_SIMULATOR)
    // Off-thread writes keep failing? Fall back to the LOOP-TASK write — the
    // empirically reliable path (the reboot-time persist always succeeds):
    // when this task writes, nothing else interleaves radio/display traffic
    // on the shared SPI bus between the SD transactions. Costs one UI hitch
    // per flush while degraded; a single success resets the counter and the
    // next flush goes back to the async task. The threshold is a setting
    // (Settings -> General; default 2, 0 = never fall back).
    const uint8_t sync_after = touchPrefsGetHistSyncAfter();
    if (sync_after && s_msgs_write_fails >= sync_after) {
      if (saveMsgsToStorage()) { _msgs_dirty = false; return; }
      _next_msgs_flush_ms = now + (s_msgs_write_fails >= 5 ? 300000 : 5000);
      return;
    }
    // A failed boot migration means the old-format file is still the on-disk
    // truth — segments must not be written until it converts (they would
    // shadow it in the loader's precedence order). The sync drain retries
    // the migration.
    if (!s_seg_store_ready) {
      if (saveMsgsToStorage()) { _msgs_dirty = false; return; }
      _next_msgs_flush_ms = now + 60000;   // store is sick; don't thrash the bus
      return;
    }
    // Card remounted (possibly a different/blank card): rebuild the segment
    // table from the ring — stale on-disk segments are removed and every
    // chunk is marked compact-dirty, so the normal job flow below re-lands
    // the whole history one bounded segment at a time, off-thread.
    if (s_seg_resync) {
      segRetableFromRing(_messages->ring().records, _messages->capacity(), _messages->ring().count, _messages->ring().head,
                         _messages->latestSequence());
      s_seg_resync = false;
    }
    // Build the next off-thread job: pending-append batch first, else the
    // oldest compact-dirty segment. The snapshot is at most ONE segment's
    // records (~80 KB) — not the old 1.3 MB whole-ring copy.
    SegJob job{};
    const int armed = segBuildJob(_messages->ring().records, _messages->capacity(), _messages->ring().count, _messages->ring().head,
                                  segEnsureBuf(&s_segjob_buf), &job);
    if (armed == 0) {
      // Everything durable. A resync's leftover files can go now that every
      // table entry has a real, current file on disk.
      if (segMoreWorkPending(_messages->latestSequence()) || !segFinishResync()) {
        _next_msgs_flush_ms = now + 2000;
        return;
      }
      _msgs_dirty = false;
      return;
    }
    if (armed < 0 || !ensureHistFlushTaskRunning()) {
      // No snapshot buffer / no worker — synchronous drain instead of losing
      // the flush (a rare spawn failure must not drop hours of chat).
      if (saveMsgsToStorage()) { _msgs_dirty = false; return; }
      _next_msgs_flush_ms = now + 2000;
      return;
    }
    // Arm the worker: descriptor written strictly before req (the worker
    // reads it only after seeing req).
    s_segjob_kind      = job.kind;
    s_segjob_first_seq = job.first_seq;
    s_segjob_last_seq  = job.last_seq;
    s_segjob_create    = job.create;
    s_segjob_repair    = job.repair;
    s_segjob_n         = job.n;
    s_segjob_redirty   = false;
    (void)_worker.queue();
    // One job armed for the worker. A multi-segment backlog (burst bigger
    // than one segment, or several dirty segments) keeps _msgs_dirty set and
    // reschedules shortly; each cycle moves one segment.
    if (segMoreWorkPending(_messages->latestSequence()) || s_seg_stale_purge) {
      _next_msgs_flush_ms = now + 1500;
      return;
    }
    _msgs_dirty = false;
    return;
#else
    if (saveMsgsToStorage()) _msgs_dirty = false;
    else _next_msgs_flush_ms = now + 2000;
#endif
  }
}


bool HistoryService::loadThreadsFromStorage() {
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  uiDataRemove(k_ui_threads_tmp_path);   // sweep an orphaned tmp from an interrupted save (mirrors the msgs-tmp sweep)
  File f = uiDataOpen(k_ui_threads_path, "r");
  if (!f) return false;

  UiHistoryHeader hdr{};
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr)) ||
      hdr.magic != k_ui_threads_magic ||
      hdr.version < k_ui_history_min_version || hdr.version > k_ui_history_version) {
    f.close(); uiDataRemove(k_ui_threads_path); return false;   // corrupt header — quarantine so it can't wedge every boot
  }
  const size_t disk_sz =
      (hdr.version >= 5 && hdr.thread_rec_size) ? hdr.thread_rec_size : sizeof(UiHistoryThread);
  if (disk_sz == 0 || disk_sz > 4096) { f.close(); uiDataRemove(k_ui_threads_path); return false; }
  // Restore the companion message counter carried here for the segmented msgs
  // store (older writers left it 0; the msgs-file loaders overwrite it anyway
  // when they run, and the mesh re-asserts it with every message).
  if (hdr.msgcount) (*_msgcount) = static_cast<int>(hdr.msgcount);

  UiHistoryThread t{};
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!readHistoryRec(f, &t, sizeof(t), disk_sz)) { f.close(); uiDataRemove(k_ui_threads_path); return false; }   // truncated/corrupt record
    UIThread value{};
    value.used              = t.used != 0;
    value.channel           = t.channel != 0;
    value.unread            = t.unread;
    value.last_ts           = t.last_ts;
    value.mesh_contact_idx  = t.mesh_contact_idx;
    memcpy(value.mesh_contact_pub,  t.mesh_contact_pub,  sizeof(t.mesh_contact_pub));
    memcpy(value.mesh_contact_key6, t.mesh_contact_key6, sizeof(t.mesh_contact_key6));
    value.mesh_channel_slot = t.mesh_channel_slot;
    strncpy(value.name, t.name, MAX_THREAD_NAME);
    value.name[MAX_THREAD_NAME] = '\0';
    _messages->restoreThread(i, value);
  }
  f.close();

  restoreSelection(hdr.active_thread_idx, hdr.active_thread_is_channel != 0);
  return true;
#else
  return false;
#endif
}

// Both legacy formats describe a *disk* ring. Its capacity may differ from
// this boot's RAM allocation. Import chronologically, keeping the newest tail.
bool HistoryService::restoreMessageRing(File& file, size_t recordSize, int slots, int count, int head) {
  if (slots < 0 || count < 0 || count > slots || head < 0 ||
      (slots ? head >= slots : head != 0)) return false;
  _messages->resetMessages();
  if (!slots) return _messages->restoreRingState(0, 0);
  const int kept = count > _messages->capacity() ? _messages->capacity() : count;
  const int drop = count - kept;
  const int oldest = (head - count + slots) % slots;
  UiHistoryMsg disk{};
  for (int i = 0; i < slots; ++i) {
    if (!readHistoryRec(file, &disk, sizeof disk, recordSize)) return false;
    const int rank = (i - oldest + slots) % slots;
    if (rank < drop || rank >= count) continue;
    UIMessage decoded{};
    decodeLegacy(disk, &decoded);
    if (!_messages->restoreMessage(rank - drop, decoded)) return false;
  }
  return _messages->restoreRingState(kept, kept % _messages->capacity());
}

bool HistoryService::loadMsgsFromStorage() {
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  File f = uiDataOpen(k_ui_msgs_path, "r");
  if (!f) return false;

  UiMsgFileHeader hdr{};
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr)) ||
      hdr.magic != k_ui_msgs_magic ||
      hdr.version < k_ui_history_min_version || hdr.version > k_ui_history_version) {
    f.close(); uiDataRemove(k_ui_msgs_path); return false;   // corrupt header — quarantine so it can't crash/wedge every boot
  }
  const size_t disk_sz =
      (hdr.version >= 5 && hdr.msg_rec_size) ? hdr.msg_rec_size : sizeof(UiHistoryMsg);
  if (disk_sz == 0 || disk_sz > 4096) { f.close(); uiDataRemove(k_ui_msgs_path); return false; }

  // The file carries the WRITER's full ring (one record per slot), so its slot
  // count is derived from the file size — NOT from this build's capacity. That
  // lets a 500-slot file load into a 5000 ring (SD upgrade) and a 5000-slot file
  // shrink into a 500 ring (card pulled), instead of quarantining real history.
  const int file_slots = (int)(((size_t)f.size() - sizeof(hdr)) / disk_sz);
  const bool restored = restoreMessageRing(f, disk_sz, file_slots, hdr.ui_msg_count, hdr.ui_msg_head);
  f.close();
  if (!restored) { uiDataRemove(k_ui_msgs_path); return false; }
  (*_msgcount)     = static_cast<int>(hdr.msgcount);
  return true;
#else
  return false;
#endif
}

// Segmented-store loader (generation 3). Reads every discovered segment
// oldest-first into the ring, quarantining corrupt segments INDIVIDUALLY (the
// single-file store threw the entire history away on one bad record), keeping
// the newest <= cap records when the on-disk set is larger than this boot's
// ring (card written under the deep cap, loaded under the small one), and
// leaving the ring LINEAR (slot 0 = oldest, head = n) — the same post-load
// invariant every ring consumer relies on. A committed empty store is also
// authoritative: falling back would resurrect a retained legacy combined file.
bool HistoryService::loadMsgsFromSegments() {
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  uint32_t seqs[k_ui_seg_max];
  const int nseg = uiSegScan(seqs, k_ui_seg_max, true /*sweep orphaned tmps*/);
  s_seg_count = 0;
  s_seg_total_bytes = 0;
  if (nseg < 0) return false;
  // Commit marker missing => this segment set is an unfinished migration (a
  // power cut between chunk writes). Wipe it and let the caller fall back to
  // the old-format file, which migrateRingToSegments only deletes after full
  // verification.
  {
    File okf = uiDataOpen(k_ui_seg_ok, FILE_READ);
    static const char expected[] = "segstore v1\n";
    char marker[sizeof expected - 1]{};
    const bool have_marker = okf && okf.readBytes(marker, sizeof marker) == sizeof marker &&
                             !memcmp(marker, expected, sizeof marker);
    if (okf) okf.close();
    if (!have_marker) {
      for (int i = 0; i < nseg; ++i) uiSegRemoveFile(seqs[i]);
      return false;
    }
  }

  WdtHeavyGuard _wg;   // ~20 segment reads on a cold card can take a while
  _messages->resetMessages();
  int w = 0;           // running chronological index; ring slot = w % cap
  uint32_t max_seq = 0;
  for (int si = 0; si < nseg; ++si) {
    File f;
    uint16_t rec_sz = 0;
    if (!uiSegOpenValidated(seqs[si], f, &rec_sz)) {
      uiSegRemoveFile(seqs[si]);   // quarantine THIS segment only
      continue;
    }
    UiSegInfo info{};
    info.first_seq = seqs[si];
    UIMessage rec;
    uint32_t seq = 0;
    uint16_t nrec = 0;
    bool     skipped = false;   // duplicates dropped -> file != table -> rewrite it
    while (uiSegReadRec(f, rec_sz, &rec, &seq)) {   // stops at EOF or a crash-ragged tail
      // Strictly monotonic seq filter. Segments are read in ascending
      // first_seq order, so a record that does not advance the sequence is a
      // duplicate or an out-of-order leftover (an interrupted repair, a stale
      // file a crash left behind, a re-appended batch) — dropping it keeps the
      // ring chronological and idempotent no matter what the disk holds.
      if (max_seq == UINT32_MAX || (rec.seq != 0 && rec.seq <= max_seq)) { skipped = true; continue; }
      if (rec.seq == 0) rec.seq = max_seq + 1;      // defensive backfill (pre-seq record)
      max_seq = rec.seq;
      info.last_seq = rec.seq;
      _messages->restoreMessage(w % _messages->capacity(), rec);
      ++w;
      ++nrec;
    }
    f.close();
    info.disk_recs = nrec;
    info.live_recs = nrec;
    info.bytes     = (uint32_t)sizeof(UiSegHeader) + (uint32_t)nrec * (uint32_t)rec_sz;
    // A segment written at a DIFFERENT record width can be READ fine (the header is
    // self-describing, and readHistoryRec prefix-reads or skips the surplus), but it
    // must never be APPENDED to: the append writes sizeof(UiSegMsg) while the header
    // still says rec_sz, so every record after the appended one is read at the wrong
    // stride — silent garbage bubbles, a poisoned seq (which then drops every later
    // segment), and the next compaction makes it permanent. Only a NON-FULL segment
    // can ever receive an append, so flag just those for a full rewrite; step 0 of
    // segBuildJob runs that before any append, and the rewrite lays down a fresh
    // header at the current width. A full old-width segment is left alone — it is
    // read correctly by its own header and costs nothing.
    if ((size_t)rec_sz != sizeof(UiSegMsg) && nrec < k_ui_seg_records) info.rewrite_open = 1;
    // The file holds records the table doesn't account for — rewrite it so disk
    // and table agree again (also drops the dead weight for good).
    if (skipped) info.rewrite_open = 1;
    if (nrec == 0) { uiSegRemoveFile(seqs[si]); continue; }   // header-only husk — reclaim
    if (s_seg_count < k_ui_seg_max) s_seg[s_seg_count++] = info;
  }

  const int total  = w;
  const int n_keep = total > _messages->capacity() ? _messages->capacity() : total;
  _messages->finishChronologicalRestore(total, max_seq + 1);

  // Reconcile the table with any dropped-oldest records: fully-dropped
  // segments violate the disk == ring invariant and are deleted outright; the
  // boundary segment keeps its file but is marked compact-dirty so the next
  // compaction (rewritten from the RAM ring) trims it to the kept records.
  const uint32_t min_kept_seq = n_keep > 0 ? _messages->ring().records[0].seq : 0;
  int out = 0;
  uint32_t total_bytes = 0;
  for (int i = 0; i < s_seg_count; ++i) {
    if (n_keep == 0 || s_seg[i].last_seq < min_kept_seq) {
      uiSegRemoveFile(s_seg[i].first_seq);
      continue;
    }
    if (s_seg[i].first_seq < min_kept_seq) s_seg[i].compact_dirty = 1;
    total_bytes += s_seg[i].bytes;
    s_seg[out++] = s_seg[i];
  }
  s_seg_count = out;
  s_seg_total_bytes = total_bytes;

  s_seg_flushed_seq = max_seq;   // everything loaded IS on disk by definition
  // (*_msgcount) isn't stored in segments; the threads-file header carries it and
  // the mesh layer re-asserts it with every message. Keep it monotonic vs seq.
  if ((int)max_seq > (*_msgcount)) (*_msgcount) = (int)max_seq;
  s_seg_store_ready = true;
  return true;
#else
  return false;
#endif
}

// One-time migration: the ring was just loaded from an OLD format (split v6
// single file or the legacy combined file) — write it out as segments, read
// every segment back and verify counts + seq bounds, and only then delete the
// old msgs file. Any failure rolls the segment set back completely so the
// intact old file stays authoritative on the next boot (a partial segment set
// would otherwise shadow it in the loader's precedence order).
bool HistoryService::migrateRingToSegments() {
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  Serial.println("[chatstore] migration: starting");
  uiDataEnsureDirs();
  // The marker goes away FIRST: from here until the verified end of this
  // migration the on-disk segment set is provisional, and a boot that finds it
  // without the marker wipes it and re-reads the (still intact) old file.
  uiDataRemove(k_ui_seg_ok);
  {   // clear any earlier/partial set so leftovers can't survive as extra segments
    uint32_t old_seqs[k_ui_seg_max];
    const int old_n = uiSegScan(old_seqs, k_ui_seg_max, true);
    for (int i = 0; i < old_n; ++i) uiSegRemoveFile(old_seqs[i]);
  }
  s_seg_count = 0;
  s_seg_total_bytes = 0;
  MessageTypes::UIMessage* buf = segEnsureBuf(&s_segsync_buf);
  if (!buf) {
    // Silent until now, and it is the first thing that can fail. A Tanmatsu
    // stuck on "migration pending" with Last save FAIL produced a boot capture
    // with NO migration lines at all, which is only possible if we bail before
    // any of them (#372). Say which step gave up.
    Serial.println("[chatstore] migration: no scratch buffer (allocation failed)");
    return false;
  }

  // Walk the ring CHRONOLOGICALLY and skip tombstones. Both matter: this runs
  // not only at boot (ring linear) but also as a retry after a failed boot
  // migration, by which time appendMessage may have WRAPPED the ring and the
  // user may have deleted messages. Slicing raw slots there produced segments
  // in slot order (first_seq > last_seq, scrambled history that read-back
  // verification cannot detect) and persisted tombstones as ghost records.
  bool ok = true;
  int in_chunk = 0;
  for (int i = 0; i <= _messages->ring().count && ok; ++i) {
    const bool flush_chunk = (i == _messages->ring().count) || (in_chunk == k_ui_seg_records);
    if (flush_chunk && in_chunk > 0) {
      const uint32_t fseq = buf[0].seq;
      ok = uiSegCompactWrite(fseq, buf, in_chunk, true /*sync-writer tmp*/);
      if (ok && s_seg_count < k_ui_seg_max) {
        UiSegInfo info{};
        info.first_seq = fseq;
        info.last_seq  = buf[in_chunk - 1].seq;
        info.disk_recs = info.live_recs = (uint16_t)in_chunk;
        info.bytes     = (uint32_t)sizeof(UiSegHeader)
                         + (uint32_t)in_chunk * (uint32_t)sizeof(UiSegMsg);
        s_seg[s_seg_count++] = info;
      }
      in_chunk = 0;
    }
    if (i == _messages->ring().count) break;
    const int slot = (_messages->ring().head - _messages->ring().count + i + _messages->capacity()) % _messages->capacity();
    if (!_messages->ring().records[slot].thread[0]) continue;              // tombstone — never persisted
    buf[in_chunk++] = _messages->ring().records[slot];
  }
  if (ok) {
    // Read-back verification: every segment must reproduce its exact record
    // count and seq bounds before the old file may be deleted.
    for (int i = 0; ok && i < s_seg_count; ++i) {
      File f;
      uint16_t rec_sz = 0;
      if (!uiSegOpenValidated(s_seg[i].first_seq, f, &rec_sz)) { ok = false; break; }
      UIMessage r;
      uint32_t seq = 0, first = 0, last = 0;
      uint16_t cnt = 0;
      bool ordered = true;
      while (uiSegReadRec(f, rec_sz, &r, &seq)) {
        if (cnt == 0) first = seq;
        else if (seq <= last) ordered = false;   // seqs must strictly increase within a segment
        last = seq;
        ++cnt;
      }
      f.close();
      ok = (ordered && cnt == s_seg[i].disk_recs &&
            first == s_seg[i].first_seq &&
            last == s_seg[i].last_seq);
    }
    // Segments must also be ordered relative to each other.
    for (int i = 1; ok && i < s_seg_count; ++i)
      ok = (s_seg[i].first_seq > s_seg[i - 1].last_seq);
  }
  // The marker commits both the message segments and their thread index. In
  // particular, the combined legacy format has no separate index yet.
  if (ok) ok = saveThreadsToStorage();
  if (!ok) {
    Serial.printf("[chatstore] migration: verification or thread index write FAILED across %d segment(s); "
                  "old file kept, will retry\n", s_seg_count);
    for (int i = 0; i < s_seg_count; ++i) uiSegRemoveFile(s_seg[i].first_seq);
    s_seg_count = 0;
    s_seg_total_bytes = 0;
    return false;   // old file untouched; the scheduler retries the migration later
  }
  // Verified. Commit: marker, then drop the old-format files.
  {
    File okf = uiDataOpen(k_ui_seg_ok, "w");
    if (!okf) {   // can't commit -> leave the old file authoritative and retry later
      Serial.println("[chatstore] migration: cannot create the commit marker; old file kept");
      for (int i = 0; i < s_seg_count; ++i) uiSegRemoveFile(s_seg[i].first_seq);
      s_seg_count = 0;
      s_seg_total_bytes = 0;
      return uiMsgsWriteFail('m');
    }
    static const char marker[] = "segstore v1\n";
    const bool committed = okf.write(reinterpret_cast<const uint8_t*>(marker), sizeof marker - 1) == sizeof marker - 1;
    okf.close();
    if (!committed) {
      uiDataRemove(k_ui_seg_ok);
      for (int i = 0; i < s_seg_count; ++i) uiSegRemoveFile(s_seg[i].first_seq);
      s_seg_count = 0;
      s_seg_total_bytes = 0;
      return uiMsgsWriteFail('m');
    }
  }
  uint32_t total_bytes = 0;
  for (int i = 0; i < s_seg_count; ++i) total_bytes += s_seg[i].bytes;
  s_seg_total_bytes = total_bytes;
  uiDataRemove(k_ui_msgs_path);
  uiDataRemove(k_ui_msgs_tmp_path);
  uiDataRemove(k_ui_msgs_tmp2_path);
  s_seg_flushed_seq = s_seg_count ? s_seg[s_seg_count - 1].last_seq : 0;
  s_seg_resync      = false;   // a full write IS the resync
  s_seg_stale_purge = false;
  s_seg_store_ready = true;
  Serial.printf("[chatstore] migration: OK, %d segment(s), %u bytes\n",
                s_seg_count, (unsigned)s_seg_total_bytes);
  return true;
#else
  return false;
#endif
}

bool HistoryService::loadLegacyHistoryFromStorage() {
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  File f = uiDataOpen(k_ui_history_path, "r");
  if (!f) return false;

  UiHistoryHeader hdr{};
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    f.close(); return false;
  }
  if (hdr.magic != k_ui_history_magic ||
      hdr.version < k_ui_history_min_version || hdr.version > k_ui_history_version) {
    f.close(); return false;
  }

  // On-disk record sizes. v5+ stores them so a blob written by an older build
  // (shorter records — fields appended since) still loads: we read the stored
  // size and leave the appended tail zeroed. A v4 blob predates these fields
  // but shares the current record layout, so fall back to sizeof(). Reject
  // absurd sizes from a corrupt blob.
  const size_t disk_thread_sz =
      (hdr.version >= 5 && hdr.thread_rec_size) ? hdr.thread_rec_size : sizeof(UiHistoryThread);
  const size_t disk_msg_sz =
      (hdr.version >= 5 && hdr.msg_rec_size) ? hdr.msg_rec_size : sizeof(UiHistoryMsg);
  if (disk_thread_sz == 0 || disk_thread_sz > 4096 ||
      disk_msg_sz == 0 || disk_msg_sz > 4096) {
    f.close(); return false;
  }

  UiHistoryThread t{};
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!readHistoryRec(f, &t, sizeof(t), disk_thread_sz)) { f.close(); return false; }
    UIThread value{};
    value.used              = t.used != 0;
    value.channel           = t.channel != 0;
    value.unread            = t.unread;
    value.last_ts           = t.last_ts;
    value.mesh_contact_idx  = t.mesh_contact_idx;
    memcpy(value.mesh_contact_pub,  t.mesh_contact_pub,  sizeof(t.mesh_contact_pub));
    memcpy(value.mesh_contact_key6, t.mesh_contact_key6, sizeof(t.mesh_contact_key6));
    value.mesh_channel_slot = t.mesh_channel_slot;
    strncpy(value.name, t.name, MAX_THREAD_NAME);
    value.name[MAX_THREAD_NAME] = '\0';
    _messages->restoreThread(i, value);
  }

  const size_t remaining = f.size() > f.position() ? f.size() - f.position() : 0;
  const int diskSlots = static_cast<int>(remaining / disk_msg_sz);
  const bool restored = restoreMessageRing(f, disk_msg_sz, diskSlots, hdr.ui_msg_count, hdr.ui_msg_head);
  f.close();
  if (!restored) return false;
  (*_msgcount)                 = static_cast<int>(hdr.msgcount);
  restoreSelection(hdr.active_thread_idx, hdr.active_thread_is_channel != 0);
  return true;
#else
  return false;
#endif
}

bool HistoryService::loadHistoryFromStorage() {
  if (!_messages || !_messages->ready()) return false;
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  const bool have_threads = loadThreadsFromStorage();
  // Format precedence: segments (generation 3) > split v6 single file >
  // legacy combined file. The older loaders are retained as migration
  // readers; whenever one of them supplies the ring, migrateRingToSegments()
  // below converts it (verify-then-delete, rollback on any failure).
  const bool from_segments = loadMsgsFromSegments();
  bool have_msgs = from_segments;
  if (!from_segments) {
    // Single-file-era hygiene: a crash mid-flush can orphan a temp file;
    // they're never read, just reclaim the space. (Segment tmps are swept by
    // the scan inside loadMsgsFromSegments.)
    uiDataRemove(k_ui_msgs_tmp_path);
    uiDataRemove(k_ui_msgs_tmp2_path);
    have_msgs = loadMsgsFromStorage();
    if (!have_msgs) {
      // A failed migration may have saved the split index before its marker.
      // Until messages are committed, the combined file remains authoritative.
      File combined = uiDataOpen(k_ui_history_path, "r");
      const bool have_combined = static_cast<bool>(combined);
      combined.close();
      if ((!have_threads || have_combined) && !loadLegacyHistoryFromStorage()) return false;
    }
  }
  // Clear persisted unread counts for threads whose messages are no longer in
  // the ring. The display ring is a fixed-size cache independent of the unread
  // counter; if the device rebooted after ring overflow the counter can be
  // non-zero while the ring holds nothing for that thread, producing a phantom
  // badge that opens to an empty chat. Resetting here is safe: the user never
  // saw those messages anyway (ring had already evicted them before the save).
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_messages->thread(i).used && _messages->thread(i).unread > 0 &&
        !_messages->threadHasMessageHistory(i)) {
      _messages->markThreadRead(i);
    }
  }
  if (!from_segments) {
    // Backfill per-record sequence numbers for records loaded from the
    // pre-segment formats (their records carry no seq on disk): stamp them in
    // chronological order, seed the generator past them, then convert the
    // ring to segments. A failed migration leaves the old file authoritative
    // and s_seg_store_ready false — the flush scheduler retries it before it
    // will write any segment.
    _messages->resequenceMessages();
    migrateRingToSegments();
  }
  return true;
#else
  return false;
#endif
}

bool HistoryService::saveThreadsToStorage() {
  if (!_messages || !_messages->ready()) return false;
#if defined(ESP32) || defined(GUARD_SIMULATOR)
#if defined(HAS_TDISPLAY_P4)
  // #167: the threads file is rewritten on every contact add/delete and chat-state change --
  // hop the write to the core-0 storage task like every other hot writer (see p4StorageCall).
  if (!p4OnStorageTask()) {
    struct A { HistoryService* t; bool ok; } a{ this, false };
    p4StorageCall([](void* p){ auto* a = (A*)p; a->ok = a->t->saveThreadsToStorage(); }, &a);
    return a.ok;
  }
#endif
  WdtHeavyGuard _wg;
  // Write to the tmp, commit with rename: a power cut mid-write leaves the old
  // index intact instead of a short file the next boot would quarantine.
  File f = uiDataOpen(k_ui_threads_tmp_path, "w");
  if (!f) return false;

  UiHistoryHeader hdr{};
  hdr.magic                 = k_ui_threads_magic;
  hdr.version               = k_ui_history_version;
  hdr.thread_rec_size       = static_cast<uint16_t>(sizeof(UiHistoryThread));
  const Selection selected = _host.selection ? _host.selection(_host.selectionContext) : Selection();
  hdr.active_thread_idx     = static_cast<int16_t>(selected.index);
  hdr.active_thread_is_channel = selected.channel ? 1u : 0u;
  // The segmented msgs store doesn't carry the companion message counter —
  // this (frequently-rewritten, small) file does instead.
  hdr.msgcount              = static_cast<uint32_t>((*_msgcount));
  if (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    f.close(); uiDataRemove(k_ui_threads_tmp_path); return false;
  }

  UiHistoryThread t{};
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    memset(&t, 0, sizeof(t));
    t.used               = _messages->thread(i).used ? 1u : 0u;
    t.channel            = _messages->thread(i).channel ? 1u : 0u;
    t.unread             = _messages->thread(i).unread;
    t.last_ts            = _messages->thread(i).last_ts;
    t.mesh_contact_idx   = _messages->thread(i).mesh_contact_idx;
    memcpy(t.mesh_contact_pub,  _messages->thread(i).mesh_contact_pub,  sizeof(t.mesh_contact_pub));
    memcpy(t.mesh_contact_key6, _messages->thread(i).mesh_contact_key6, sizeof(t.mesh_contact_key6));
    t.mesh_channel_slot  = _messages->thread(i).mesh_channel_slot;
    strncpy(t.name, _messages->thread(i).name, MAX_THREAD_NAME);
    t.name[MAX_THREAD_NAME] = '\0';
    if (f.write(reinterpret_cast<const uint8_t*>(&t), sizeof(t)) != sizeof(t)) {
      f.close(); uiDataRemove(k_ui_threads_tmp_path); return false;
    }
  }
  f.close();
  return uiDataReplaceFile(k_ui_threads_path, k_ui_threads_tmp_path);
#else
  return false;
#endif
}

// Write a message ring (the live one at shutdown, the worker's snapshot for the
// periodic flush) to the history file. Chunked writer (same pattern that fixed
// the contacts stall, #82): records are packed into an INTERNAL-RAM chunk and
// flushed in ~6 KB writes, so the ring costs ~20 FS calls (500 slots) / ~210
// (5000 slots) instead of one small write per record. This is NOT the "one big
// write from PSRAM" that regressed before: the chunk lives in internal RAM (the
// flash driver never bounces a PSRAM source). Alloc failure falls back to the
// original per-record writes.
// Record a ring-write outcome (runs on the loop task OR the core-0 worker —
// atomic diagnostic fields, LVGL-free) and stamp the SD health probe on failure so
// a wedged card gets arbitrated instead of the write failing silently forever.
bool HistoryService::uiMsgsWriteResult(bool ok) {
  const uint32_t m = (uint32_t)millis();
  const time_t   now_t = time(nullptr);
  const uint32_t ep = (_host.clockCurrent() &&
                       now_t > (time_t)ClockFloorRTC::MIN_VALID_EPOCH)
                          ? (uint32_t)now_t : 0;
  if (ok) {
    s_msgs_write_ok_ms = m ? m : 1;
    s_msgs_write_ok_epoch = ep;
    s_msgs_write_fails = 0;
  } else {
    s_msgs_write_fail_ms = m ? m : 1;
    s_msgs_write_fail_epoch = ep;
    if (s_msgs_write_fails < 0xFFFFu) s_msgs_write_fails = s_msgs_write_fails + 1;
#if defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO) || defined(TLORA_PAGER) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4_R8)
    if (_host.isSd()) if (_host.ioFailure) _host.ioFailure();
#endif
  }
  return ok;
}
// Failure wrapper: record WHERE the write died and the errno — 'open failed
// with ENFILE' (VFS file-handle table full) needs a completely different fix
// than 'body write failed with EIO' (card), and without this they were
// indistinguishable on a device with no readable serial.
bool HistoryService::uiMsgsWriteFail(char stage) {
  s_msgs_write_stage = (uint8_t)stage;
  s_msgs_write_errno = errno;
  return uiMsgsWriteResult(false);
}

// ---- Segmented store: low-level file ops --------------------------------
// Pure file-layer primitives (no scheduling, no table state — that lives with
// the flush scheduler). All take explicit record arrays (snapshots or the
// ring under the loop task's ownership) and report through the same
// uiMsgsWriteResult/uiMsgsWriteFail health machinery as the old writer, so
// the About diagnostics, failure toasts, sync-fallback and SD-wedge
// arbitration keep working unchanged. Stage codes: 'a' append, 'c' compact,
// 'd' data-dir create, 's' segment scan (plus errno).

// Root-relative segment file name: "/msgs/seg_<first_seq>.bin<suffix>".
// Worst case "/msgs/seg_4294967295.bin.tm2" = 28 chars — fits uiDataOpen's
// 80-byte path buffer and SPIFFS' 31-char flat-name limit.
void HistoryService::uiSegName(uint32_t first_seq, const char* suffix, char* out, size_t cap) {
  snprintf(out, cap, "%s/seg_%lu.bin%s", k_ui_seg_dir, (unsigned long)first_seq, suffix ? suffix : "");
}

// Ensure the data root + segment dir exist. FAT backends (SD /meshcomod,
// SD_MMC /meshcomod, FFat) need real directories; SPIFFS has a flat namespace
// where mkdir fails harmlessly and slash-in-name files just work — so this is
// best-effort by design. Called at backend resolve and from every remount
// path (a fresh replacement card has neither directory).
void HistoryService::uiDataEnsureDirs() {
  if (!uiDataFsReady()) return;
  ui::history::FileStore<fs::FS>(*_host.filesystem(), _host.root()).ensureDirectory(k_ui_seg_dir);
}

// Marshal one RAM record into the on-disk segment record (drops the RAM-only
// fields, carries seq). Mirrors the old writer's field list exactly.


// Chunked record writer shared by append + compact: marshals through a small
// INTERNAL-RAM buffer (the flash/SD drivers must never see a PSRAM source
// pointer), per-record fallback when the heap is tight. Returns false on any
// short write (errno left for the caller's stage report).
bool HistoryService::uiSegWriteRecords(File& f, const MessageTypes::UIMessage* recs, int n) {
  const size_t REC = sizeof(UiSegMsg);
  size_t chunk_recs = 6144 / REC;
  if (chunk_recs < 1) chunk_recs = 1;
  uint8_t* buf = (uint8_t*)malloc(REC * chunk_recs);   // internal RAM by default
  bool ok = true;
  if (buf) {
    size_t fill = 0;
    for (int k = 0; ok && k < n; ++k) {
      ui::history::encode(recs[k], reinterpret_cast<UiSegMsg*>(buf + fill));
      fill += REC;
      if (fill == REC * chunk_recs) {
        ok = (f.write(buf, fill) == fill);
        fill = 0;
      }
    }
    if (ok && fill > 0) ok = (f.write(buf, fill) == fill);
    free(buf);
  } else {
    UiSegMsg rec;
    for (int k = 0; ok && k < n; ++k) {
      ui::history::encode(recs[k], &rec);
      ok = (f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec)) == sizeof(rec));
    }
  }
  return ok;
}

// Append records to a segment file. `create` = the file does not exist yet
// (write the header first). Driven by table state, NOT by File::size() — the
// Tanmatsu FFat metadata layer lies about sizes. A crash mid-append leaves a
// ragged tail the loader truncates to the record boundary; nothing else is at
// risk (the previous records and every other segment are untouched — this is
// the whole point of the segmented layout).
bool HistoryService::uiSegAppendRecords(uint32_t first_seq, bool create,
                               const MessageTypes::UIMessage* recs, int n) {
#if defined(HAS_TDISPLAY_P4)
  // #167: history writes hop to the core-0 storage task (see p4StorageCall in DataStore.cpp) --
  // a core-1 SD write can mask the DSI frame-restart ISR long enough to drop a display frame.
  if (!p4OnStorageTask()) {
    struct A { HistoryService* owner; uint32_t fs; bool c; const MessageTypes::UIMessage* r; int n; bool ok; } a{ this, first_seq, create, recs, n, false };
    p4StorageCall([](void* p){ auto* a = (A*)p; a->ok = a->owner->uiSegAppendRecords(a->fs, a->c, a->r, a->n); }, &a);
    return a.ok;
  }
#endif
  WdtHeavyGuard _wg;
  errno = 0;
  char name[48];
  uiSegName(first_seq, "", name, sizeof name);
  // create: open truncating — a stale file under the same key (crash residue)
  // must not end up with a second header appended mid-file.
  const char* mode = create ? "w" : FILE_APPEND;
  File f = uiDataOpen(name, mode);
  if (!f) {
    // First failure on a fresh card/dir is usually a missing parent dir —
    // create it and retry once before reporting.
    uiDataEnsureDirs();
    errno = 0;
    f = uiDataOpen(name, mode);
    if (!f) return uiMsgsWriteFail('a');
  }
  bool ok = true;
  if (create) {
    UiSegHeader hdr{};
    hdr.magic        = k_ui_seg_magic;
    hdr.version      = k_ui_seg_version;
    hdr.msg_rec_size = (uint16_t)sizeof(UiSegMsg);
    hdr.first_seq    = first_seq;
    ok = (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr));
  }
  if (ok) ok = uiSegWriteRecords(f, recs, n);
  f.close();
  if (!ok) {
    // A short write can have landed whole records already. The file content is
    // now untrusted: appending the retry after that tail would duplicate (or,
    // if the failure split a record, MISALIGN) records the loader accepts as
    // history. Flag the segment so the scheduler repairs it with a full
    // rewrite before any further append. Publish only the segment identity;
    // the UI scheduler owns the table and applies this after the job finishes.
    _repair_segment.store(first_seq, std::memory_order_release);
    if (create) uiSegRemoveFile(first_seq);   // nothing valid in it yet
    return uiMsgsWriteFail('a');
  }
  return uiMsgsWriteResult(true);
}

// Rewrite one segment from the given (live, tombstone-free) records — tmp +
// rename, same crash discipline as the old writer but with a one-segment
// blast radius. n == 0 removes the segment file outright (everything in it
// was deleted). `sync_writer` picks the tmp namespace: the loop-task sync
// writer must never share a tmp with a possibly-stalled worker write (two
// truncating opens of one path interleave into garbage — the .tmp/.tm2
// lesson from the single-file store).
bool HistoryService::uiSegCompactWrite(uint32_t first_seq, const MessageTypes::UIMessage* recs, int n,
                              bool sync_writer) {
#if defined(HAS_TDISPLAY_P4)
  if (!p4OnStorageTask()) {   // #167: hop to core 0 (see uiSegAppendRecords)
    struct A { HistoryService* owner; uint32_t fs; const MessageTypes::UIMessage* r; int n; bool sw; bool ok; } a{ this, first_seq, recs, n, sync_writer, false };
    p4StorageCall([](void* p){ auto* a = (A*)p; a->ok = a->owner->uiSegCompactWrite(a->fs, a->r, a->n, a->sw); }, &a);
    return a.ok;
  }
#endif
  WdtHeavyGuard _wg;
  errno = 0;
  char fin[48], tmp[48];
  uiSegName(first_seq, "", fin, sizeof fin);
  if (n <= 0) {
    uiDataRemove(fin);
    return uiMsgsWriteResult(true);
  }
  uiSegName(first_seq, sync_writer ? ".tm2" : ".tmp", tmp, sizeof tmp);
  File f = uiDataOpen(tmp, "w");
  if (!f) {
    uiDataEnsureDirs();
    errno = 0;
    f = uiDataOpen(tmp, "w");
    if (!f) return uiMsgsWriteFail('c');
  }
  UiSegHeader hdr{};
  hdr.magic        = k_ui_seg_magic;
  hdr.version      = k_ui_seg_version;
  hdr.msg_rec_size = (uint16_t)sizeof(UiSegMsg);
  hdr.first_seq    = first_seq;
  bool ok = (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr));
  if (ok) ok = uiSegWriteRecords(f, recs, n);
  f.close();
  if (!ok) { uiDataRemove(tmp); return uiMsgsWriteFail('c'); }
  errno = 0;
  if (!uiDataReplaceFile(fin, tmp)) return uiMsgsWriteFail('c');
  return uiMsgsWriteResult(true);
}

// Remove a retired segment (ring grew past retention; its records are gone
// from RAM too).
void HistoryService::uiSegRemoveFile(uint32_t first_seq) {
#if defined(HAS_TDISPLAY_P4)
  if (!p4OnStorageTask()) {   // #167: hop to core 0 (see uiSegAppendRecords)
    struct A { HistoryService* owner; uint32_t seq; } a{this, first_seq};
    p4StorageCall([](void* p){ auto* a = static_cast<A*>(p); a->owner->uiSegRemoveFile(a->seq); }, &a);
    return;
  }
#endif
  char name[48];
  uiSegName(first_seq, "", name, sizeof name);
  uiDataRemove(name);
}

// Open + validate a segment for reading. Returns the on-disk record size via
// *rec_size_out (self-describing header) and leaves the File positioned at
// the first record. Returns false on a corrupt header — the caller
// quarantines THAT segment only.
bool HistoryService::uiSegOpenValidated(uint32_t first_seq, File& f, uint16_t* rec_size_out) {
  char name[48];
  uiSegName(first_seq, "", name, sizeof name);
  f = uiDataOpen(name, FILE_READ);
  if (!f) return false;
  UiSegHeader hdr{};
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != (int)sizeof(hdr) ||
      hdr.magic != k_ui_seg_magic ||
      hdr.version == 0 || hdr.version > k_ui_seg_version ||
      hdr.msg_rec_size == 0 || hdr.msg_rec_size > 4096 ||
      hdr.first_seq != first_seq) {
    f.close();
    return false;
  }
  *rec_size_out = hdr.msg_rec_size;
  return true;
}

// Read ONE record from an open segment into a RAM record. Returns false at a
// clean EOF or a ragged (crash-truncated) tail — the caller just stops there;
// records already read are valid. Size-agnostic via readHistoryRec (older
// shorter records zero-fill their tail, so a missing seq field reads 0 and
// the loader backfills it).
bool HistoryService::uiSegReadRec(File& f, uint16_t disk_sz, MessageTypes::UIMessage* out, uint32_t* seq_out) {
  UiSegMsg rec{};
  if (!readHistoryRec(f, &rec, sizeof(rec), disk_sz)) return false;
  ui::history::decode(rec, out);
  *seq_out = rec.seq;
  return true;
}

// Discover segment files: fills out_first_seqs sorted ascending, returns the
// count (or -1 when even the scan location can't be opened — distinct from
// "no segments yet"). Handles both directory layouts: a real <root>/msgs dir
// (FAT backends) and SPIFFS' flat namespace (scan "/" and prefix-match the
// name). When sweep_tmps is set, orphaned .tmp/.tm2 leftovers from a crash
// mid-compact are deleted along the way (boot hygiene).
int HistoryService::uiSegScan(uint32_t* out_first_seqs, int max_out, bool sweep_tmps) {
  if (!uiDataFsReady()) return -1;
  char dirpath[80];
  snprintf(dirpath, sizeof dirpath, "%s%s", _host.root(), k_ui_seg_dir);
  File dir = _host.filesystem()->open(dirpath);
  bool flat = false;
  if (!dir || !dir.isDirectory()) {
    // SPIFFS (flat namespace): enumerate the root and match the name prefix.
    if (dir) dir.close();
    flat = true;
    dir = _host.filesystem()->open(_host.root()[0] ? _host.root() : "/");
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return -1;
    }
  }
  const size_t root_len = strlen(_host.root());
  int n = 0;
  for (;;) {
    String path = dir.getNextFileName();
    if (path.length() == 0) break;
    // Normalize to a root-relative name so uiDataRemove/uiDataOpen accept it.
    const char* rel = path.c_str();
    if (root_len && strncmp(rel, _host.root(), root_len) == 0) rel += root_len;
    const char* base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    if (flat) {
      // Flat scan sees every file — keep only "/msgs/seg_*" names.
      if (strncmp(rel, k_ui_seg_dir, strlen(k_ui_seg_dir)) != 0) continue;
    }
    if (strncmp(base, "seg_", 4) != 0) continue;
    char* end = nullptr;
    const unsigned long fs_val = strtoul(base + 4, &end, 10);
    if (!end || end == base + 4) continue;
    if (strcmp(end, ".bin") == 0) {
      if (n < max_out) out_first_seqs[n++] = (uint32_t)fs_val;
    } else if (sweep_tmps && (strcmp(end, ".bin.tmp") == 0 || strcmp(end, ".bin.tm2") == 0)) {
      uiDataRemove(rel);
    }
  }
  dir.close();
  // Insertion sort ascending — n is tiny (<= k_ui_seg_max).
  for (int i = 1; i < n; ++i) {
    const uint32_t v = out_first_seqs[i];
    int j = i - 1;
    while (j >= 0 && out_first_seqs[j] > v) { out_first_seqs[j + 1] = out_first_seqs[j]; --j; }
    out_first_seqs[j + 1] = v;
  }
  return n;
}

// ---- Segmented store: flush scheduling ------------------------------------
// All of this state is LOOP-TASK-owned; the hist_flush worker only runs
// uiSegRunArmedJob() and reports through s_hist_flush_ok.

// Snapshot buffer for one job (<= one segment of records). Lazy, kept for the
// session — two exist (worker vs sync drain) so the writers can never share.
MessageTypes::UIMessage* HistoryService::segEnsureBuf(MessageTypes::UIMessage** slot) {
  if (!*slot) {
    const size_t sz = sizeof(MessageTypes::UIMessage) * (size_t)k_ui_seg_records;
    *slot = (MessageTypes::UIMessage*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*slot) *slot = (MessageTypes::UIMessage*)heap_caps_malloc(sz, MALLOC_CAP_8BIT);
  }
  return *slot;
}

// Live records with seq in [lo, hi], chronological, from the ring.
int HistoryService::segGatherRange(const MessageTypes::UIMessage* ring, int cap, int count, int head,
                          uint32_t lo, uint32_t hi, MessageTypes::UIMessage* out, int max_out) {
  int n = 0;
  for (int i = 0; i < count && n < max_out; ++i) {
    const int slot = (head - count + i + cap) % cap;
    const MessageTypes::UIMessage& m = ring[slot];
    if (!m.thread[0]) continue;               // tombstone
    if (m.seq < lo || m.seq > hi) continue;
    out[n++] = m;
  }
  return n;
}

// Build the next job into `buf` + `out`. PURE with respect to the worker
// descriptor — arming (copying the snapshot + publishing Queued) is the async
// caller's step, so the sync drain can use this with its own buffer and a
// local job without the worker ever seeing it. Returns 1 = job built,
// 0 = nothing pending (all durable), -1 = no buffer.
int HistoryService::segBuildJob(const MessageTypes::UIMessage* ring, int cap, int count, int head,
                       MessageTypes::UIMessage* buf, SegJob* out) {
  if (!buf) return -1;
  const uint32_t repair = _repair_segment.exchange(0, std::memory_order_acq_rel);
  if (repair) for (int i = 0; i < s_seg_count; ++i)
    if (s_seg[i].first_seq == repair) { s_seg[i].rewrite_open = 1; break; }
  // 0) REPAIR FIRST. A segment whose file content is untrusted (failed append
  //    left a partial tail, or a resync left it stale/absent) must be fully
  //    rewritten before anything appends to it — otherwise a retry appends the
  //    same batch after the partial tail (duplicate or misaligned records the
  //    loader would ingest as history) or creates a HEADERLESS file. The
  //    rewrite absorbs the unflushed tail too, bounded by the segment size;
  //    whatever doesn't fit stays above the watermark and lands as a normal
  //    append into the NEXT segment afterwards.
  for (int i = 0; i < s_seg_count; ++i) {
    if (!s_seg[i].rewrite_open) continue;
    const uint32_t hi = (i + 1 < s_seg_count) ? (s_seg[i + 1].first_seq - 1) : 0xFFFFFFFFu;
    const int rn = segGatherRange(ring, cap, count, head, s_seg[i].first_seq, hi,
                                  buf, k_ui_seg_records);
    out->kind      = SEGJOB_COMPACT;   // a compact IS the repair (tmp + rename, header written)
    out->first_seq = s_seg[i].first_seq;
    out->last_seq  = rn > 0 ? buf[rn - 1].seq : 0;
    out->create    = false;
    out->n         = rn;               // 0 -> unlink (every record in the range is gone)
    out->repair    = true;             // commit also clears rewrite_open + advances the watermark
    return 1;
  }
  // 1) Pending appends: live records newer than the durability watermark,
  //    chronological, capped to the room left in the active segment.
  bool create = true;
  uint32_t target = 0;
  int room = k_ui_seg_records;
  if (s_seg_count > 0 && s_seg[s_seg_count - 1].disk_recs < k_ui_seg_records) {
    create = false;
    target = s_seg[s_seg_count - 1].first_seq;
    room   = k_ui_seg_records - (int)s_seg[s_seg_count - 1].disk_recs;
  }
  // At the table cap (s_seg_count == k_ui_seg_max) a CREATE-append cannot be
  // recorded: segCommitJob only tables a create while s_seg_count < the cap, so
  // the file it writes becomes an untabled orphan AND the durability watermark
  // never advances -> the same append re-arms every cycle (permanent write-spin),
  // orphaned seg_*.bin pile up as the ring shifts the target first_seq, and the
  // card eventually fills ("SD I/O fails after a few hours"). DEFER the append:
  // skip step 1 entirely (crucially, do NOT run the gather/watermark advance
  // below on un-written records) and fall through to step 2, which retires the
  // oldest segment first. At the cap the oldest 256 records have aged out of the
  // 5000-record ring (24*256 disk slots > ring), so that segment is fully
  // evicted -> compact_dirty (segNoteEvicted) -> step 2 unlinks it and frees a
  // slot with NO live-record loss. The deferred records are the NEWEST (above
  // the watermark, so the ring evicts them last); they wait safely in RAM and
  // land next cycle once a slot is free. Below the cap this is a no-op.
  if (!(create && s_seg_count >= k_ui_seg_max)) {
    int n = 0;
    uint32_t last = s_seg_flushed_seq;
    for (int i = 0; i < count; ++i) {
      const int slot = (head - count + i + cap) % cap;
      const MessageTypes::UIMessage& m = ring[slot];
      if (m.seq <= s_seg_flushed_seq) continue;
      if (m.thread[0]) {
        if (n >= room) break;                   // segment full — the rest rides the next cycle
        buf[n++] = m;
      }
      if (m.seq > last) last = m.seq;           // deleted-before-flush records advance the
                                                // watermark with no write (never hit disk)
    }
    if (n > 0) {
      out->kind      = SEGJOB_APPEND;
      out->first_seq = create ? buf[0].seq : target;
      out->last_seq  = last;
      out->create    = create;
      out->n         = n;
      return 1;
    }
    if (last > s_seg_flushed_seq) s_seg_flushed_seq = last;   // pure-tombstone tail
  }
  // 2) Oldest compact-dirty segment: rewrite it from the ring's live records.
  for (int i = 0; i < s_seg_count; ++i) {
    if (!s_seg[i].compact_dirty) continue;
    const int cn = segGatherRange(ring, cap, count, head,
                                  s_seg[i].first_seq, s_seg[i].last_seq,
                                  buf, k_ui_seg_records);
    out->kind      = SEGJOB_COMPACT;
    out->first_seq = s_seg[i].first_seq;
    out->last_seq  = cn > 0 ? buf[cn - 1].seq : 0;
    out->create    = false;
    out->n         = cn;                      // 0 = every record gone -> unlink the file
    return 1;
  }
  return 0;
}

// Apply a successfully-executed job to the table (loop task only).
void HistoryService::segCommitJob(const SegJob& job) {
  if (job.kind == SEGJOB_APPEND) {
    if (job.create && s_seg_count < k_ui_seg_max) {
      UiSegInfo info{};
      info.first_seq = job.first_seq;
      info.bytes     = (uint32_t)sizeof(UiSegHeader);
      s_seg[s_seg_count++] = info;
      s_seg_total_bytes = s_seg_total_bytes + (uint32_t)sizeof(UiSegHeader);
    }
    // Credit the job to ITS OWN segment (looked up by key), never blindly to
    // the last table entry — the table can have changed between arm and commit.
    for (int i = 0; i < s_seg_count; ++i) {
      if (s_seg[i].first_seq != job.first_seq) continue;
      UiSegInfo& a = s_seg[i];
      a.disk_recs = (uint16_t)(a.disk_recs + job.n);
      a.live_recs = (uint16_t)(a.live_recs + job.n);
      if (job.last_seq > a.last_seq) a.last_seq = job.last_seq;
      const uint32_t add = (uint32_t)job.n * (uint32_t)sizeof(UiSegMsg);
      a.bytes += add;
      s_seg_total_bytes = s_seg_total_bytes + add;
      if (job.last_seq > s_seg_flushed_seq) s_seg_flushed_seq = job.last_seq;
      break;
    }
  } else if (job.kind == SEGJOB_COMPACT) {
    for (int i = 0; i < s_seg_count; ++i) {
      if (s_seg[i].first_seq != job.first_seq) continue;
      s_seg_total_bytes = (s_seg_total_bytes >= s_seg[i].bytes)
                              ? s_seg_total_bytes - s_seg[i].bytes : 0;
      if (job.n == 0) {
        for (int k = i; k + 1 < s_seg_count; ++k) s_seg[k] = s_seg[k + 1];
        --s_seg_count;
      } else {
        s_seg[i].disk_recs = s_seg[i].live_recs = (uint16_t)job.n;
        s_seg[i].last_seq  = job.last_seq;
        s_seg[i].bytes     = (uint32_t)sizeof(UiSegHeader)
                             + (uint32_t)job.n * (uint32_t)sizeof(UiSegMsg);
        // A delete that landed while this compact was writing predates
        // nothing: the snapshot was taken BEFORE it, so the record is still
        // in the file — keep the segment dirty for a follow-up pass.
        s_seg[i].compact_dirty = s_segjob_redirty ? 1 : 0;
        s_seg_total_bytes  = s_seg_total_bytes + s_seg[i].bytes;
        if (job.repair) {
          // The file now matches the table exactly, so it is trustworthy again
          // and the records it absorbed from the unflushed tail are durable.
          s_seg[i].rewrite_open = 0;
          if (job.last_seq > s_seg_flushed_seq) s_seg_flushed_seq = job.last_seq;
        }
      }
      break;
    }
  }
}

bool HistoryService::segMoreWorkPending(uint32_t newest_seq) {
  if (newest_seq > s_seg_flushed_seq) return true;
  for (int i = 0; i < s_seg_count; ++i)
    if (s_seg[i].compact_dirty || s_seg[i].rewrite_open) return true;
  return false;
}

// Executed by the hist_flush worker (or inline by the sync drain's caller).
bool HistoryService::uiSegRunArmedJob() {
  switch (s_segjob_kind) {
    case SEGJOB_APPEND:
      return uiSegAppendRecords(s_segjob_first_seq, s_segjob_create, s_segjob_buf, s_segjob_n);
    case SEGJOB_COMPACT:
      return uiSegCompactWrite(s_segjob_first_seq, s_segjob_buf, s_segjob_n, false);
    default:
      return true;
  }
}

// A record with this seq was tombstoned (deleted) — mark its segment for
// compaction. Unflushed records (seq above the watermark) need nothing: the
// append builder skips tombstones, so they simply never reach disk.
void HistoryService::segMarkSeqDirty(uint32_t seq) {
  if (seq == 0 || seq > s_seg_flushed_seq) return;
  for (int i = 0; i < s_seg_count; ++i) {
    if (seq >= s_seg[i].first_seq && seq <= s_seg[i].last_seq) {
      s_seg[i].compact_dirty = 1;
      // Racing an in-flight compact of this very segment: its snapshot was
      // taken before this delete, so the commit must not clear the flag.
      if (s_segjob_kind == SEGJOB_COMPACT && s_segjob_first_seq == s_seg[i].first_seq &&
          (_worker.active()))
        s_segjob_redirty = true;
      return;
    }
  }
}

// The ring overwrote its oldest record (capacity eviction). Deliberately does
// NOT trigger a rewrite per eviction (that would be write-amplification per
// message all over again): the segment file only gets unlinked once ALL its
// records have aged out; a partially-evicted boundary segment keeps its stale
// tail on disk until the next boot's loader trims it (bounded: one segment).
void HistoryService::segNoteEvicted(uint32_t seq) {
  if (seq == 0) return;
  for (int i = 0; i < s_seg_count; ++i) {
    if (seq < s_seg[i].first_seq || seq > s_seg[i].last_seq) continue;
    if (s_seg[i].live_recs > 0) --s_seg[i].live_recs;
    if (s_seg[i].live_recs == 0) s_seg[i].compact_dirty = 1;   // job -> unlink (n gathers to 0)
    return;
  }
}

// Card remounted (wedge recovery / reinsert / FM insert / post-format): the
// on-disk segment set may be stale, foreign, or gone. Rebuild the table from
// the ring's live records with every chunk marked UNTRUSTED, so the normal job
// flow rewrites each one (header + records, tmp + rename) one bounded segment
// at a time.
//
// Deliberately does NOT delete anything up front: on a same-card recovery the
// existing files still hold the very history we are re-landing, and unlinking
// them first turned recovery into a multi-minute window where a power cut lost
// data that had been perfectly durable. Same-key files are simply replaced by
// each repair's rename; anything left over (different chunk boundaries, or a
// genuinely foreign card) is swept once the re-land is complete — and the
// loader's seq-monotonic filter makes a leftover harmless even if we crash
// before that sweep.
void HistoryService::segRetableFromRing(const MessageTypes::UIMessage* ring, int cap, int count, int head,
                               uint32_t newest_seq) {
  uiDataEnsureDirs(); // replacement cards also need the empty-store commit path
  s_seg_count = 0;
  s_seg_total_bytes = 0;
  UiSegInfo cur{};
  int in_chunk = 0;
  for (int i = 0; i < count; ++i) {
    const int slot = (head - count + i + cap) % cap;
    const MessageTypes::UIMessage& m = ring[slot];
    if (!m.thread[0]) continue;
    if (in_chunk == 0) {
      cur = UiSegInfo{};
      cur.first_seq    = m.seq;
      cur.rewrite_open = 1;       // file content untrusted -> repair rewrites it
    }
    cur.last_seq = m.seq;
    ++in_chunk;
    if (in_chunk == k_ui_seg_records) {
      cur.live_recs = (uint16_t)in_chunk;
      if (s_seg_count < k_ui_seg_max) s_seg[s_seg_count++] = cur;
      in_chunk = 0;
    }
  }
  if (in_chunk > 0 && s_seg_count < k_ui_seg_max) {
    cur.live_recs = (uint16_t)in_chunk;
    s_seg[s_seg_count++] = cur;
  }
  s_seg_flushed_seq = newest_seq;
  s_seg_stale_purge = true;   // sweep leftovers once every chunk has landed
}

// Unlink segment files that are not part of the table. Runs only when no work
// is pending (so every table entry has a real, current file) — see the purge
// rationale on segRetableFromRing.
void HistoryService::segPurgeStaleFiles() {
  uint32_t seqs[k_ui_seg_max];
  const int nseg = uiSegScan(seqs, k_ui_seg_max, true);
  if (nseg < 0) return;                       // scan location unreadable — try again later
  for (int i = 0; i < nseg; ++i) {
    bool keep = false;
    for (int k = 0; k < s_seg_count; ++k)
      if (s_seg[k].first_seq == seqs[i]) { keep = true; break; }
    if (!keep) uiSegRemoveFile(seqs[i]);
  }
  // A failed remove must not publish a fresh-card commit with foreign segments
  // still present. Retain the resync work until the directory agrees.
  const int remaining = uiSegScan(seqs, k_ui_seg_max, true);
  if (remaining < 0) return;
  for (int i = 0; i < remaining; ++i) {
    bool keep = false;
    for (int k = 0; k < s_seg_count; ++k)
      if (s_seg[k].first_seq == seqs[i]) { keep = true; break; }
    if (!keep) return;
  }
  s_seg_stale_purge = false;
}

// A remount may select a blank/formatted card. Repairing its segments alone is
// insufficient: the boot loader accepts them only with an index and marker.
// Called after all repair jobs have completed, including the empty-ring case.
bool HistoryService::segFinishResync() {
  if (!s_seg_stale_purge) return true;
  if (!saveThreadsToStorage()) return false;
  segPurgeStaleFiles();
  if (s_seg_stale_purge) return false;
  static const char marker[] = "segstore v1\n";
  File existing = uiDataOpen(k_ui_seg_ok, "r");
  char bytes[sizeof marker - 1]{};
  const bool committed = existing && existing.readBytes(bytes, sizeof bytes) == sizeof bytes &&
                         !memcmp(bytes, marker, sizeof bytes);
  if (existing) existing.close();
  if (committed) return true; // Keep a same-card recovery's existing marker intact.
  File file = uiDataOpen(k_ui_seg_ok, "w");
  const bool ok = file && file.write(reinterpret_cast<const uint8_t*>(marker), sizeof marker - 1)
                              == sizeof marker - 1;
  if (file) file.close();
  if (!ok) {
    uiDataRemove(k_ui_seg_ok);
    s_seg_stale_purge = true;
    return uiMsgsWriteFail('m');
  }
  return true;
}

bool HistoryService::saveMsgsToStorage() {
  if (!_messages || !_messages->ready()) return false;
#if defined(ESP32) || defined(GUARD_SIMULATOR)
  // Synchronous drain of ALL outstanding message-store work on the LOOP task —
  // shutdown/reboot, the delete flows (persistHistoryNow), the sync-fallback
  // and the no-worker fallback all land here. Loop-task writes are the
  // empirically reliable path on the shared SPI bus. Returns true only when
  // everything is durable.
  if (!uiDataFsReady()) return false;
  // A worker that outlived uiHistWaitWorkerIdle's 9 s cap still holds a segment
  // file OPEN. Writing here would remove/rename a path under that open handle:
  // with FF_FS_LOCK=0 the stalled handle keeps writing into clusters the FS has
  // freed and may re-allocate, cross-linking chains and corrupting the volume.
  // Report failure instead (the caller surfaces "Chat history save FAILED");
  // the data stays in the RAM ring and the segment is already flagged for a
  // repair rewrite by uiHistWaitWorkerIdle.
  if (_worker.active()) return false;
  if (s_segjob_kind != SEGJOB_NONE) uiHistWaitWorkerIdle();
  if (!s_seg_store_ready) {
    // A failed boot migration left the old-format file authoritative —
    // converting it IS the flush (verify-then-delete inside).
    return migrateRingToSegments();
  }
  if (s_seg_resync) {
    segRetableFromRing(_messages->ring().records, _messages->capacity(), _messages->ring().count, _messages->ring().head,
                       _messages->latestSequence());
    s_seg_resync = false;
  }
  // Drain job by job, executing each here. Appends are NEVER FILE_APPEND on
  // this path: a worker stalled past uiHistWaitWorkerIdle's 9 s cap could
  // still hold an append handle on the same file, and two appenders
  // interleave records — instead the whole (bounded, <= ~60 KB) active
  // segment is rewritten from the ring via the sync tmp namespace, where
  // rename-over is last-writer-wins with each candidate internally
  // consistent.
  MessageTypes::UIMessage* buf = segEnsureBuf(&s_segsync_buf);   // NEVER the worker's buffer:
  // a worker stalled past the 9 s idle-wait cap may still be reading its own.
  for (int guard = 0; guard < k_ui_seg_max + 4; ++guard) {
    SegJob job{};
    const int armed = segBuildJob(_messages->ring().records, _messages->capacity(), _messages->ring().count, _messages->ring().head, buf, &job);
    if (armed == 0) {
      // segBuildJob returns 0 for "all durable" AND for a create-append it had to
      // DEFER at the segment cap with no slot freeable this pass (rare
      // fragmentation edge — the common at-cap path retires the oldest evicted
      // segment inside this loop and resolves). Only the former is saved; report
      // the latter as a failure so the caller surfaces it and retries, instead of
      // dropping the newest records on a false success. Normal case:
      // segMoreWorkPending is false here, so this is a no-op.
      if (segMoreWorkPending(_messages->latestSequence())) return false;
      return segFinishResync();
    }
    if (armed < 0) return false;               // no snapshot buffer
    bool ok;
    if (job.kind == SEGJOB_APPEND) {
      if (job.create && s_seg_count < k_ui_seg_max) {
        UiSegInfo info{};
        info.first_seq = job.first_seq;
        s_seg[s_seg_count++] = info;
      }
      const int n2 = segGatherRange(_messages->ring().records, _messages->capacity(), _messages->ring().count, _messages->ring().head,
                                    job.first_seq, job.last_seq,
                                    buf, k_ui_seg_records);
      ok = uiSegCompactWrite(job.first_seq, buf, n2, true);
      if (ok) {
        const uint32_t newest = job.last_seq;
        job.kind = SEGJOB_COMPACT;             // commit with absolute-value semantics
        job.n    = n2;
        segCommitJob(job);
        if (newest > s_seg_flushed_seq) s_seg_flushed_seq = newest;
      }
    } else {
      ok = uiSegCompactWrite(job.first_seq, buf, job.n, true);
      if (ok) segCommitJob(job);
    }
    if (!ok) return false;
  }
  return !segMoreWorkPending(_messages->latestSequence()) && segFinishResync();
#else
  return false;
#endif
}


// Two writers interleaving on the history file would corrupt it: wait out an
// in-flight worker flush (bounded; worst observed SPIFFS GC ~6-8 s) and cancel
// a pending one. Returns true if a pending flush was cancelled — its snapshot
// was never written, so the caller must treat the ring as dirty and write it.
bool HistoryService::uiHistWaitWorkerIdle() {
  const bool cancelled = _worker.cancel();
  const uint8_t  had_kind = s_segjob_kind;
  const uint32_t had_seq  = s_segjob_first_seq;
  const uint32_t t0 = millis();
  while (_worker.running() && (uint32_t)(millis() - t0) < 9000) delay(10);
  // A timed-out worker still owns the descriptor and buffer. Never alter them
  // until it publishes idle; callers refuse synchronous writes while active.
  if (_worker.active()) return cancelled;
  // Drop the completed/cancelled descriptor: the caller is about to write
  // synchronously from the ring, and leaving it armed let the next
  // flushHistoryIfDue observe a stale job and commit it a SECOND time
  // (double-counted record/byte totals, or a duplicate table entry).
  s_segjob_kind    = SEGJOB_NONE;
  s_segjob_redirty = false;
  // If the job was already in the worker's hands its fate is now unknown — it
  // may have written some or all of its records without being credited. Flag
  // the target segment untrusted so the synchronous drain REWRITES it from the
  // ring instead of appending on top (which would duplicate whatever landed).
  if (had_kind != SEGJOB_NONE && !cancelled) {
    for (int i = 0; i < s_seg_count; ++i)
      if (s_seg[i].first_seq == had_seq) { s_seg[i].rewrite_open = 1; break; }
  }
  return cancelled;
}


#if defined(ESP32)
// ---- Chat-history flush worker (core 1) --------------------------------------
// The flush used to ride the core-0 tile_fetch worker — the Wi-Fi core. Under
// Wi-Fi load its prio-23 driver tasks starve a prio-1 task for hundreds of ms
// at a time, and sd_diskio's busy-waits measure WALL time: the timeout expires
// while the task simply wasn't scheduled, so perfectly good SD writes surface
// as EIO ("card I/O error") — while the identical write from the core-1 loop
// task (the reboot-time persist) succeeds every time. Chat persistence has no
// Wi-Fi affinity, so it gets its own tiny task pinned to core 1 at the loop
// task's priority (equal-priority time-slicing keeps the UI serviced during a
// write). The task is persistent; the 100 ms poll of the atomic job state
// costs nothing measurable and mirrors the old worker's pickup latency.
void HistoryService::histFlushTaskFn(void* context) {
  static_cast<HistoryService*>(context)->runWorker();
}

void HistoryService::runWorker() {
  for (;;) {
    runPendingWrite();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
bool HistoryService::ensureHistFlushTaskRunning() {
  if (_host.startWriter) return _host.startWriter();
  if (s_hist_flush_task != nullptr) return true;
  if (!s_hist_flush_stack) {
    // The segment writers keep their bulk buffers on the heap; stack use is
    // File objects + FatFs path work. 6 KB leaves ample headroom (the tile
    // worker's overflow-into-globals history earned the caution).
    static const size_t k_sz = 6 * 1024;
    s_hist_flush_stack = (StackType_t*)heap_caps_malloc(k_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_hist_flush_stack) return false;
    s_hist_flush_stack_bytes = k_sz;
  }
  s_hist_flush_task = xTaskCreateStaticPinnedToCore(
      histFlushTaskFn, "hist_flush",
      s_hist_flush_stack_bytes / sizeof(StackType_t),
      this, 1,
      s_hist_flush_stack, &s_hist_flush_tcb,
      1 /*core 1 — NOT the Wi-Fi core; see block comment*/);
  return s_hist_flush_task != nullptr;
}


#else
bool HistoryService::ensureHistFlushTaskRunning() { return _host.startWriter && _host.startWriter(); }
#endif

bool HistoryService::runPendingWrite() {
  ui::platform::StorageLease lease;
  if (!lease.acquired()) return false;
  if (!_worker.claim()) return false;
  s_hist_flush_ok = uiSegRunArmedJob();
  _worker.finish();
  return true;
}

} }
