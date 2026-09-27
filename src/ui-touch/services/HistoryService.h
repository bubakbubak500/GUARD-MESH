// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageStore.h"
#include "HistoryFormat.h"
#include "HistoryWorkerState.h"
#include <FS.h>
#include <atomic>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace ui { namespace history {
// Owns history scheduling, segment table, job snapshots, worker and diagnostics.
// The UI/model remain on the loop task. Storage is borrowed through Host;
// this service never mounts a card or touches an LVGL object.
class HistoryService : public MessageTypes {
public:
  struct Selection {
    int index;
    bool channel;
    Selection(int value = -1, bool isChannel = false) : index(value), channel(isChannel) {}
  };
  struct Host {
    fs::FS* (*filesystem)();
    const char* (*root)();
    bool (*isSd)();
    bool (*clockCurrent)();
    void (*ioFailure)();
    void (*alert)(const char*, int);
    bool (*startWriter)(); // optional executor; it calls runPendingWrite() until stopped
    // Selection belongs to the chat session. Persistence exchanges values,
    // never writable aliases to another owner's state.
    void* selectionContext = nullptr;
    Selection (*selection)(void*) = nullptr;
    void (*restoreSelection)(void*, Selection) = nullptr;
  };
  struct Status {
    uint32_t okMs, okEpoch, failEpoch, bytes;
    uint16_t failures;
    int segments;
    bool ready;
  };
  HistoryService() = default;
  ~HistoryService();
  HistoryService(const HistoryService&) = delete;
  HistoryService& operator=(const HistoryService&) = delete;
  void configure(MessageStore& messages, int& counter, Host host);
  Status status() const;
  bool busy() const { return _worker.running(); }
  bool active() const { return _worker.active(); }
  bool pending() const { return _worker.pending(); }
  bool threadsDirty() const { return _threads_dirty; }
  bool messagesDirty() const { return _msgs_dirty; }
  void resetSchedule() { _threads_dirty = _msgs_dirty = false; _next_threads_flush_ms = _next_msgs_flush_ms = 0; }
  void forceMessagesDirty() { _msgs_dirty = true; }
  void threadWriteCompleted(bool ok) { _threads_dirty = !ok; if (!ok) _next_threads_flush_ms = millis() + 200; }
  void chatSaveFailText(char* out, size_t cap);
  void markThreadsDirty(unsigned long delay_ms = 200);
  void markMsgsDirty(unsigned long delay_ms = 2000);
  void flushHistorySoon();
  void flushHistoryIfDue(unsigned long now);
  bool loadHistoryFromStorage();
  bool saveThreadsToStorage();
  void uiDataEnsureDirs();
  void segMarkSeqDirty(uint32_t seq);
  void segNoteEvicted(uint32_t seq);
  bool saveMsgsToStorage();
  bool uiHistWaitWorkerIdle();
  // Worker-side entry point: claims one immutable snapshot, publishes completion.
  // External executors must be joined before destroying this service.
  bool runPendingWrite();
private:
  Host _host{};
  MessageStore* _messages = nullptr;
  void restoreSelection(int index, bool channel);
  int* _msgcount = nullptr;
  bool _threads_dirty = false, _msgs_dirty = false;
  unsigned long _next_threads_flush_ms = 0, _next_msgs_flush_ms = 0;
// UI -> worker: write the chat-history snapshot to storage. The full-file write
// can stall for multiple seconds in SPIFFS garbage collection on SD-less boards
// (Heltec V4); on the loop thread that froze the whole UI, incl. touch wake
// (the "ui:hist 6140ms" field stall). The loop thread snapshots the ring, the
// worker writes the snapshot; shutdown/reboot still write synchronously.
HistoryWorkerState _worker;
std::atomic<uint32_t> _repair_segment{0};
std::atomic<bool> s_hist_flush_ok{true};   // last worker write result (retry on false)
// Message-ring write health. A failing ui_msgs write used to be 100% silent —
// the user only found out on the next reboot as "messages from the last N
// minutes vanished" (the thread list still showed fresh times because the
// small threads file kept writing while the big ring write failed). Updated
// inside the segment writers from EITHER task (atomic diagnostic fields); read by
// the About page + the repeated-failure toast in flushHistoryIfDue.
std::atomic<uint32_t> s_msgs_write_ok_ms   {0};  // millis() of last successful ring write (0 = none yet)
std::atomic<uint32_t> s_msgs_write_fail_ms {0};  // millis() of last failed ring write
// Wall-clock epochs for the same two events, so the readouts can show WHEN a
// save happened instead of how long ago. 0 = never, or the system clock wasn't
// set yet at the time (a relative age would be meaningless anyway).
std::atomic<uint32_t> s_msgs_write_ok_epoch   {0};
std::atomic<uint32_t> s_msgs_write_fail_epoch {0};
std::atomic<uint16_t> s_msgs_write_fails   {0};  // consecutive failures since the last success
std::atomic<uint8_t>  s_msgs_write_stage   {0};  // where the last failure hit: 'o' open, 'h' header, 'b' body, 'r' rename
std::atomic<int>      s_msgs_write_errno   {0};  // errno at that failure (ENFILE/EMFILE = VFS file table full, ENOSPC = card full, EIO = card error)
// ---- Segmented store: runtime segment table (loop-task-owned) --------------
// Oldest-first view of the on-disk segment set. Rebuilt by the boot loader,
// consulted and updated by the flush scheduler. The hist_flush worker never
// touches it — it only executes fully-described jobs and reports ok/fail.
struct UiSegInfo {
  uint32_t first_seq;      // filename key + first record's seq
  uint32_t last_seq;       // newest seq physically in the FILE
  uint32_t bytes;          // file size (header + records) — arithmetic, never stat()
  uint16_t disk_recs;      // records physically in the file
  uint16_t live_recs;      // of those, how many are still live in the RAM ring
  uint8_t  compact_dirty;  // RAM tombstones/drops within this segment -> rewrite due
  // The FILE's content is untrusted: it may be missing, partially written (a
  // failed append), or stale (post-resync). Repair = a full rewrite from the
  // ring that also ABSORBS the unflushed tail, so no append may target it
  // until that lands. Set on append failure and by segRetableFromRing.
  uint8_t  rewrite_open;
};
UiSegInfo s_seg[k_ui_seg_max] = {};
int       s_seg_count = 0;
volatile uint32_t s_seg_total_bytes = 0;  // writer-maintained; About page reads it
volatile bool     s_seg_resync = false;   // card swapped/remounted: rewrite everything from RAM
// Set by segRetableFromRing: on-disk segment files that are NOT part of the
// rebuilt table must be unlinked — but only once the re-land has landed, so a
// same-card recovery keeps its durable history readable the whole time
// (deleting up front turned a recovery into a multi-minute window where a
// power cut lost everything that had been safe on disk).
bool s_seg_stale_purge = false;
// False until the boot loader read segments OR the one-time migration landed.
// While false the flush scheduler must not append segments (they would shadow
// the still-authoritative old-format file on the next boot) — it retries the
// migration instead.
bool s_seg_store_ready = false;
// Durability watermark: highest seq that has landed in a segment file. Ring
// records with seq above it are the pending-append backlog. Loop-task-owned;
// advanced only when a write is COMMITTED (worker job observed ok, or a sync
// write returned true).
uint32_t s_seg_flushed_seq = 0;

// One flush job in flight at a time (the atomic ownership protocol). Built
// and committed on the loop task; the hist_flush worker (or the sync path)
// only executes it. The snapshot buffer holds at most one segment's records
// (~80 KB PSRAM) — it replaces the old whole-ring snapshot (1.3 MB).
enum : uint8_t { SEGJOB_NONE = 0, SEGJOB_APPEND, SEGJOB_COMPACT };
struct SegJob {
  uint8_t  kind      = SEGJOB_NONE;
  uint32_t first_seq = 0;      // target segment (filename key)
  uint32_t last_seq  = 0;      // newest seq covered by the job
  bool     create    = false;  // APPEND: file doesn't exist yet (write header)
  bool     repair    = false;  // COMPACT of an untrusted file: also absorbs the unflushed tail
  int      n         = 0;
};
// The WORKER's armed job. Written only while the worker is idle; the sync drain
// uses its own local SegJob + its own buffer so a worker stalled past the 9 s
// idle-wait cap can never observe a rebuilt descriptor or a reused buffer.
uint8_t  s_segjob_kind      = SEGJOB_NONE;
uint32_t s_segjob_first_seq = 0;
uint32_t s_segjob_last_seq  = 0;
bool     s_segjob_create    = false;
bool     s_segjob_repair    = false;
int      s_segjob_n         = 0;
UIMessage* s_segjob_buf = nullptr;   // worker jobs only
UIMessage* s_segsync_buf = nullptr;  // sync-drain jobs only
// A delete landed inside the segment a COMPACT job is currently rewriting —
// its snapshot predates the tombstone, so the segment must stay dirty when
// the job commits (a second compaction picks the deletion up).
bool s_segjob_redirty = false;

  bool loadThreadsFromStorage();
  bool loadMsgsFromStorage();
  bool restoreMessageRing(File&, size_t recordSize, int slots, int count, int head);
  bool loadMsgsFromSegments();
  bool migrateRingToSegments();
  bool loadLegacyHistoryFromStorage();
  bool uiMsgsWriteResult(bool ok);
  bool uiMsgsWriteFail(char stage);
  void uiSegName(uint32_t first_seq, const char* suffix, char* out, size_t cap);
  bool uiSegWriteRecords(File& f, const UIMessage* recs, int n);
  bool uiSegAppendRecords(uint32_t first_seq, bool create,
                               const UIMessage* recs, int n);
  bool uiSegCompactWrite(uint32_t first_seq, const UIMessage* recs, int n,
                              bool sync_writer);
  void uiSegRemoveFile(uint32_t first_seq);
  bool uiSegOpenValidated(uint32_t first_seq, File& f, uint16_t* rec_size_out);
  bool uiSegReadRec(File& f, uint16_t disk_sz, UIMessage* out, uint32_t* seq_out);
  int uiSegScan(uint32_t* out_first_seqs, int max_out, bool sweep_tmps);
  UIMessage* segEnsureBuf(UIMessage** slot);
  int segGatherRange(const UIMessage* ring, int cap, int count, int head,
                          uint32_t lo, uint32_t hi, UIMessage* out, int max_out);
  int segBuildJob(const UIMessage* ring, int cap, int count, int head,
                       UIMessage* buf, SegJob* out);
  void segCommitJob(const SegJob& job);
  bool segMoreWorkPending(uint32_t newest_seq);
  bool uiSegRunArmedJob();
  void segRetableFromRing(const UIMessage* ring, int cap, int count, int head,
                               uint32_t newest_seq);
  void segPurgeStaleFiles();
  bool segFinishResync();
  static void histFlushTaskFn(void* context);
  bool ensureHistFlushTaskRunning();
  void runWorker();
  bool uiDataFsReady() { return _host.filesystem && _host.filesystem(); }
  bool uiDataFsIsSdCard() { return _host.isSd && _host.isSd(); }
  File uiDataOpen(const char* name, const char* mode);
  void uiDataRemove(const char* name);
  bool uiDataReplaceFile(const char* name, const char* temporary);
#if defined(ESP32)
TaskHandle_t s_hist_flush_task        = nullptr;
StaticTask_t s_hist_flush_tcb;
StackType_t* s_hist_flush_stack       = nullptr;
size_t       s_hist_flush_stack_bytes = 0;
#endif
};
} }
