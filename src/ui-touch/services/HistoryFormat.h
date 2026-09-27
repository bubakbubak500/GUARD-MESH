// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include <stddef.h>
// Disk layout is deliberately independent of UI classes and target peripherals.
namespace ui { namespace history {
constexpr const char* k_ui_history_path = "/ui_chat_history_v1.bin";
constexpr uint32_t k_ui_history_magic = 0x55494348; // "UICH"
// Bump when the on-disk record layout changes. From v5 on, the header
// self-describes its record sizes (see loadHistoryFromStorage), so you can
// APPEND fields to the END of UiHistoryThread/UiHistoryMsg and OLD blobs still
// load — the appended tail reads back zero instead of the whole history being
// discarded. v4 added meta_flags/path_len/snr_q4/rssi per message.
constexpr uint16_t k_ui_history_version = 6;   // v6: MAX_MSG_TEXT 96 -> 160 (record size changed)
// Oldest on-disk version we still load. v4's record layout matches the current
// build, so it reads fine via the sizeof() fallback below; anything older
// predates that layout and is discarded.
constexpr uint16_t k_ui_history_min_version = 6;   // v4/v5 used 96-char records; reject (don't mis-read)

// Split-format file paths. Thread metadata and the message ring are written
// to separate files so unread counts (small, ~4 KB) can flush at 200 ms while
// the ring (larger, scales with MAX_UI_MESSAGES) flushes lazily every 2 s,
// reducing flash write pressure when the ring is large.
constexpr const char* k_ui_threads_path = "/ui_threads_v1.bin";
// Threads-index writes go tmp+rename (same discipline as the segment compacts):
// a truncate-in-place "w" rewrite interrupted by a hard power-cut left a short
// file the next boot quarantine-DELETED — chat list, unread counts and DM
// entries gone (M9 power slider = rail cut; any board on a brownout).
constexpr const char* k_ui_threads_tmp_path = "/ui_threads_v1.bin.tmp";
constexpr const char* k_ui_msgs_path    = "/ui_msgs_v1.bin";
constexpr const char* k_ui_msgs_tmp_path = "/ui_msgs_v1.bin.tmp";
// Separate temp for the SYNCHRONOUS (shutdown/reboot/no-PSRAM) writer. The
// core-0 worker and the loop task can overlap when a stalled worker outlives
// uiHistWaitWorkerIdle's 9 s cap — two truncating opens of ONE tmp interleave
// into garbage, and whichever rename lands last installs a corrupt file that
// the next boot quarantines (total history loss). Distinct tmp names make the
// overlap last-rename-wins with each candidate internally consistent.
constexpr const char* k_ui_msgs_tmp2_path = "/ui_msgs_v1.bin.tm2";
constexpr uint32_t k_ui_threads_magic   = 0x55495448;  // "UITH"
constexpr uint32_t k_ui_msgs_magic      = 0x55494D53;  // "UIMS"



struct __attribute__((packed)) UiHistoryHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint16_t ui_msg_count;
  uint16_t ui_msg_head;
  uint32_t msgcount;
  int16_t active_thread_idx;
  uint8_t active_thread_is_channel;
  // v5+: on-disk record sizes at write time, so the loader can read a blob
  // whose records were SHORTER (fields appended since) and zero-fill the new
  // tail. Zero in a v4 blob -> loader falls back to sizeof(). Carved out of the
  // old _pad, so the header size is unchanged from v4.
  uint16_t thread_rec_size;
  uint16_t msg_rec_size;
  uint8_t _pad[1];
};

struct __attribute__((packed)) UiHistoryThread {
  uint8_t used;
  uint8_t channel;
  uint16_t unread;
  uint32_t last_ts;
  int16_t mesh_contact_idx;
  uint8_t mesh_contact_pub[32];
  uint8_t mesh_contact_key6[6];
  int16_t mesh_channel_slot;
  char name[MessageTypes::MAX_THREAD_NAME + 1];
};

// On-disk width of UiHistoryMsg::sender, FROZEN at what v6 blobs were written with.
// Deliberately NOT tied to MessageTypes::MAX_SENDER_NAME: `sender` sits between `thread` and
// `text`, so growing it in place shifts `text` for every already-stored record. Nothing
// detects that — the segment loader validates only magic/version/rec_size, so an old
// record would prefix-read into the new offsets and every message would silently lose
// the first bytes of its body. A sender too long for this field spills into
// UiSegMsg::sender_ext instead (see SenderExtField.h).
constexpr int k_ui_disk_sender_len = 25;   // == the historical MAX_SENDER_NAME + 1

struct __attribute__((packed)) UiHistoryMsg {
  uint32_t ts;
  uint8_t channel;
  uint8_t outgoing;
  // v4 additions — RX metadata for the per-bubble Info popup. All zero when
  // the message was either outgoing or loaded from a pre-v4 blob (which
  // gets discarded outright on the version-mismatch check, so the latter
  // shouldn't happen in practice, but the zero defaults keep the Info
  // popup honest if it ever does).
  uint8_t meta_flags;
  uint8_t path_len;
  int8_t  snr_q4;
  int8_t  rssi;
  char thread[MessageTypes::MAX_THREAD_NAME + 1];
  char sender[k_ui_disk_sender_len];
  char text[MessageTypes::MAX_MSG_TEXT + 1];
  // Append any FUTURE fields HERE (after text) and bump k_ui_history_version.
  // The v5+ loader zero-fills appended fields for older blobs, so appending
  // never wipes chat history. Inserting in the middle (as v4 did) breaks that
  // and must instead raise k_ui_history_min_version.
};

// Header for the split message-ring file (/ui_msgs_v1.bin). The thread file
// (/ui_threads_v1.bin) reuses UiHistoryHeader (it holds active_thread_idx and
// thread_rec_size). This smaller header holds only the ring-specific fields.
struct __attribute__((packed)) UiMsgFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint16_t ui_msg_count;
  uint16_t ui_msg_head;
  uint32_t msgcount;
  uint16_t msg_rec_size;
  uint8_t  _pad[2];
};

// ---- Segmented message store (generation 3) ------------------------------
// The ring is persisted as fixed-capacity SEGMENT files under <data root>/msgs:
// seg_<first_seq>.bin, k_ui_seg_records records each. New messages APPEND one
// record to the newest ("active") segment — a ~240 B write instead of the old
// full-ring rewrite (515 KB+ at 2300 messages), which shrinks the hard-cut
// loss window, the SPI bus-collision window, and flash wear all at once.
// Deletes tombstone in RAM and mark the owning segment dirty; compaction
// rewrites JUST that segment from the RAM ring (disk holds exactly the ring's
// record set, so no read pass is ever needed). Corruption quarantines one
// segment instead of the whole history. On FAT backends /msgs is a real
// subdirectory (single child under /meshcomod — stays inside the factory
// wipe's 24-entry cap); on SPIFFS the same name is a legal FLAT file name
// (31-char limit: "/msgs/seg_4294967295.bin" = 24 chars), so the layout is
// identical everywhere and no mkdir is required there.
constexpr const char* k_ui_seg_dir     = "/msgs";
// Commit marker. Written (last) only once a migration has been fully verified,
// so its ABSENCE means "the segment set on disk is a partial migration" — the
// loader then wipes those segments and falls back to the still-intact
// old-format file. Without it, a power cut between chunk writes left a valid
// oldest-first PREFIX that shadowed the old file forever: the UI came up
// showing only the oldest few hundred messages, permanently.
constexpr const char* k_ui_seg_ok      = "/msgs/store.ok";
constexpr uint32_t    k_ui_seg_magic   = 0x55495347;   // "UISG"
constexpr uint16_t    k_ui_seg_version = 1;
constexpr int         k_ui_seg_records = 256;          // records per segment (~60 KB at v1 size)
constexpr int         k_ui_seg_max     = 24;           // table cap: 5000/256 = 20 live segments + slack

struct __attribute__((packed)) UiSegHeader {
  uint32_t magic;          // k_ui_seg_magic
  uint16_t version;        // k_ui_seg_version
  uint16_t msg_rec_size;   // sizeof(UiSegMsg) at write time — self-describing so
                           // future fields append at the END (same rule as v5+)
  uint32_t first_seq;      // seq of this segment's first-ever record (== filename key)
  uint8_t  _pad[4];
};

// On-disk segment record: the v6 record layout + the monotonic seq appended
// (append-at-end evolution — a reader of either size copies min(rec_size) and
// zero-fills the rest, so seq reads 0 from a hypothetical older record).
struct __attribute__((packed)) UiSegMsg {
  UiHistoryMsg m;
  uint32_t     seq;
  // Names run to 31 chars on the wire (MeshCore's ContactInfo::name[32]) but m.sender
  // freezes at the 24 the v6 record was written with. The tail lives HERE, appended at
  // the very end — after seq, not inside m — so every pre-existing field keeps its
  // offset and the min(rec_size) prefix read stays correct in BOTH directions: an old
  // 233-byte record zero-fills this and yields its original short name, and an older
  // firmware reading this 240-byte record copies the first 233 bytes and ignores the
  // tail (the name falls back to 24 chars, nothing else changes). That is why
  // k_ui_seg_version STAYS 1 — bumping it would make that older firmware reject the
  // file outright (uiSegOpenValidated rejects version > k_ui_seg_version) and quarantine
  // a perfectly readable history. Split/rejoin lives in SenderExtField.h.
  //
  // Rollback caveat, since two widths now exist in the wild: an older firmware READS
  // this record correctly, but if it then APPENDS to the same segment it writes 233-byte
  // records into a file whose header says 240, and everything after them reads at the
  // wrong stride. Nothing on this side can prevent that (the write is the old build's),
  // and a version bump would not help either — it would only make the old build reject
  // the file instead. Our own direction of the same hazard IS handled: see the width
  // check in loadMsgsFromSegments, which refuses to append across widths.
  char         sender_ext[MessageTypes::MAX_SENDER_NAME + 1 - k_ui_disk_sender_len];
};

// Pin what an ALREADY-WRITTEN record's reader depends on. These are offsets, not the total
// size, precisely because appending further fields at the tail stays safe — what must never
// move is anything a v6-era record already holds, since nothing on the read path would
// detect the shift. Growing the record past these offsets is fine; moving them is not.
static_assert(sizeof(UiHistoryMsg) == 229, "v6 record layout changed — old blobs would mis-read");
static_assert(offsetof(UiSegMsg, seq) == 229, "seq moved — old segments would mis-read");
static_assert(offsetof(UiSegMsg, sender_ext) == 233,
              "the appended tail must start where the old record ended, or old segments mis-read");
} }
