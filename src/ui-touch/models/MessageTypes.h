#pragma once
#include <stdint.h>
namespace ui {
struct MessageTypes {
  static const int MAX_UI_MESSAGES = 500;
  /** Deep ring for devices whose chat history lives on an SD card (T-Deck with a
   *  card, Tanmatsu SD_MMC): 10x the internal-flash ring. Chosen at begin() into
   *  _ui_msg_cap; PSRAM cost ~5000 * sizeof(UIMessage) ≈ 1.3 MB (of 8 MB). */
  static const int MAX_UI_MESSAGES_SD = 5000;
  static const int MAX_UI_THREADS = 48;
  static const int MAX_THREAD_NAME = 32;
  // Full MeshCore name width: ContactInfo::name / ChannelDetails::name / NodePrefs::
  // node_name are all char[32], so 31 chars + NUL is exactly lossless. Was 24, which
  // silently dropped every longer name. The PERSISTED width is frozen separately
  // (k_ui_disk_sender_len in UITask.cpp) — raising this does not move any on-disk field.
  static const int MAX_SENDER_NAME = 31;
  static const int MAX_MSG_TEXT = 160;   // full LoRa text length (was 96 -> cut long msgs ~3 lines)
  static const int MAX_UI_PATH = 32;  // inbound-route bytes/message for the Info popup (covers deep + multi-byte-hash routes)

  // Outgoing-DM delivery state. None for incoming + channel messages.
  enum : uint8_t {
    DELIV_NONE      = 0,  // unknown (incoming msgs, channel posts, or pre-ACK history loaded from disk)
    DELIV_SENT      = 1,  // sendMessage returned SENT_*, ack not yet received
    DELIV_DELIVERED = 2,  // ack matched in MyMesh::processAck → onMessageAcked
    DELIV_FAILED    = 3,  // sendMessage returned FAILED
  };

  // Per-message RX metadata. Populated from the LoRa packet at receive time
  // (in MyMesh::queueMessage / onChannelMessageRecv) and surfaced via the
  // bubble long-press "Info" sheet. Outgoing messages + history loaded from
  // pre-v4 disk don't have these — meta_flags bit 0 distinguishes.
  enum : uint8_t {
    MSG_META_HAS_RX    = (1u << 0),  // snr_q4/rssi/path_len populated
    MSG_META_IS_FLOOD  = (1u << 1),  // packet was flooded (path_len = hop count); else 0xFF / direct
    MSG_META_HAS_SCOPE = (1u << 2),  // in_scope holds a valid transport scope (transport_codes[0])
    // The scope code is an HMAC of THIS packet's payload under the sender's
    // region key, so it differs for every message and cannot be read as a region
    // id (#259). The only thing a receiver can say about it is whether it
    // verifies against a key we hold — which we check at RX, while the packet is
    // still around, and record here.
    MSG_META_SCOPE_HOME = (1u << 3),  // in_scope verified against OUR region key
    // Bits 4-7: which REGISTERED region in_scope verified against (#271), as a
    // RegionRegistry slot, where 0 = none/unknown, 15 = several regions matched
    // and 1..14 name one. Packed into the spare top nibble on purpose: meta_flags is
    // already persisted at a fixed offset, so this costs no record growth and no
    // history version bump, and messages written before a region was registered
    // read back 0, which is exactly the honest "unknown" state. A slot is stable
    // for the life of the history and is never a list index, so deleting or
    // reordering regions cannot relabel old messages.
    MSG_META_SCOPE_SLOT_SHIFT = 4,
    MSG_META_SCOPE_SLOT_MASK  = 0xF0,
  };
  static uint8_t metaScopeSlot(uint8_t meta_flags) {
    return (uint8_t)((meta_flags & MSG_META_SCOPE_SLOT_MASK) >> MSG_META_SCOPE_SLOT_SHIFT);
  }
  static uint8_t metaWithScopeSlot(uint8_t meta_flags, uint8_t slot) {
    return (uint8_t)((meta_flags & ~MSG_META_SCOPE_SLOT_MASK)
                     | ((slot & 0x0F) << MSG_META_SCOPE_SLOT_SHIFT));
  }

  struct UIMessage {
    uint32_t ts;
    // Monotonic per-record sequence number, assigned at append and persisted by
    // the segmented store (its record-to-segment mapping keys on seq ranges).
    // NEVER reset mid-session; the boot loader seeds the generator from
    // max(loaded seq)+1. Distinct from _msgcount, which the companion protocol
    // may overwrite (msgRead) and therefore cannot be a unique key.
    uint32_t seq;
    bool channel;
    bool outgoing;
    uint32_t ack_hash;       // expected-ack for outgoing DMs (0 if none / channel / incoming)
    uint8_t  deliv_state;    // see DELIV_* above; defaults to DELIV_NONE
    uint8_t  meta_flags;     // see MSG_META_* (0 = no RX metadata)
    uint8_t  path_len;       // hop count for flood packets; 0xFF for direct/routed; 0 if unknown
    int8_t   snr_q4;         // SNR × 4 (matches on-wire encoding); 0 if unknown
    int8_t   rssi;           // dBm; 0 if unknown
    // RAM-only (not persisted): route + repeats metadata, valid for the current
    // session. sent_fp links an outgoing flood to MyMesh's "repeats heard" ring;
    // in_path[] holds the repeater hashes an inbound flood traversed.
    uint32_t sent_fp;
    uint16_t in_scope;       // transport scope (transport_codes[0]); valid iff MSG_META_HAS_SCOPE
    uint8_t  in_path_n;
    uint8_t  in_path[MAX_UI_PATH];
    char thread[MAX_THREAD_NAME + 1];
    char sender[MAX_SENDER_NAME + 1];
    char text[MAX_MSG_TEXT + 1];
  };

  struct UIThread {
    bool used;
    bool channel;
    bool has_mention;   // an unread message in this thread @mentions me (RAM-only hint)
    uint16_t unread;
    uint32_t last_ts;
    /** Mesh contact index for DM sends, or -1 if unknown / not mapped. */
    int16_t mesh_contact_idx;
    /** Full contact pubkey for stable DM mapping across refresh/reorders. */
    uint8_t mesh_contact_pub[32];
    /** First 6 bytes of contact pubkey for stable DM mapping across reorders. */
    uint8_t mesh_contact_key6[6];
    /** Channel slot index for group sends, or -1 if unknown. */
    int16_t mesh_channel_slot;
    char name[MAX_THREAD_NAME + 1];
  };

};
}
