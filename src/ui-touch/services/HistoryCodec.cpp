#include "HistoryCodec.h"
#include "../SenderExtField.h"
#include <cstring>
namespace ui { namespace history {
void encode(const MessageTypes::UIMessage& s, UiSegMsg* d) {
  memset(d, 0, sizeof(*d));
  d->m.ts         = s.ts;
  d->m.channel    = s.channel ? 1u : 0u;
  d->m.outgoing   = s.outgoing ? 1u : 0u;
  d->m.meta_flags = s.meta_flags;
  d->m.path_len   = s.path_len;
  d->m.snr_q4     = s.snr_q4;
  d->m.rssi       = s.rssi;
  strncpy(d->m.thread, s.thread, sizeof(d->m.thread) - 1);
  d->m.thread[sizeof(d->m.thread) - 1] = '\0';
  SenderExtField::store(s.sender, d->m.sender, sizeof(d->m.sender),
                        d->sender_ext, sizeof(d->sender_ext));
  strncpy(d->m.text, s.text, sizeof(d->m.text) - 1);
  d->m.text[sizeof(d->m.text) - 1] = '\0';
  d->seq = s.seq;
}
void decode(const UiSegMsg& rec, MessageTypes::UIMessage* out) {
  memset(out, 0, sizeof(*out));
  out->ts         = rec.m.ts;
  out->channel    = rec.m.channel != 0;
  out->outgoing   = rec.m.outgoing != 0;
  out->meta_flags = rec.m.meta_flags;
  out->path_len   = rec.m.path_len;
  out->snr_q4     = rec.m.snr_q4;
  out->rssi       = rec.m.rssi;
  strncpy(out->thread, rec.m.thread, MessageTypes::MAX_THREAD_NAME);
  out->thread[MessageTypes::MAX_THREAD_NAME] = '\0';
  SenderExtField::load(rec.m.sender, sizeof(rec.m.sender),
                       rec.sender_ext, sizeof(rec.sender_ext),
                       out->sender, sizeof(out->sender));
  strncpy(out->text, rec.m.text, MessageTypes::MAX_MSG_TEXT);
  out->text[MessageTypes::MAX_MSG_TEXT] = '\0';
  out->seq = rec.seq;
}

void decodeLegacy(const UiHistoryMsg& source, MessageTypes::UIMessage* destination) {
  UiSegMsg record{};
  record.m = source;
  // Historical ring readers kept at most 24 sender bytes.
  record.m.sender[sizeof(record.m.sender) - 1] = '\0';
  decode(record, destination);
}
} }
