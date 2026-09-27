// SPDX-License-Identifier: GPL-3.0-or-later
#include "RadioService.h"
#include <cstdio>
#include <cstring>
namespace ui {
uint32_t RadioService::nextTime(RadioTransport& transport) {
  uint32_t timestamp = transport.uniqueTime();
  if (timestamp <= _last_timestamp) timestamp = _last_timestamp + 1;
  return _last_timestamp = timestamp;
}
RadioSendResult RadioService::sendChannel(RadioTransport& transport, int slot,
    const char* name, const char* sender, const char* text, Trace trace) {
  RadioSendResult result;
  if (!text || !*text || !sender) return result;
  if (!name || slot < 0 || !transport.channelMatches(slot, name)) {
    result.status = RadioSendResult::MissingChannel;
    return result;
  }
  const uint32_t timestamp = nextTime(transport);
  if (trace) {
    char line[96];
    snprintf(line, sizeof line, "TX CH ts=%lu len=%u",
             static_cast<unsigned long>(timestamp), static_cast<unsigned>(strlen(text)));
    trace(line);
  }
  return transport.channel(slot, name, timestamp, sender, text);
}
RadioSendResult RadioService::sendDirect(RadioTransport& transport, const uint8_t* key,
    const char* text, Trace trace) {
  RadioSendResult result;
  if (!text || !*text) return result;
  int path = 0;
  if (!key || !transport.contactPath(key, path)) {
    if (trace) trace("TX DM blocked: locked key missing");
    result.status = RadioSendResult::MissingContact;
    return result;
  }
  if (trace) {
    char line[96];
    snprintf(line, sizeof line, "TX DM to %02X%02X%02X%02X%02X%02X p=%d->flood len=%u",
             key[0], key[1], key[2], key[3], key[4], key[5], path,
             static_cast<unsigned>(strlen(text)));
    trace(line);
  }
  const uint32_t timestamp = nextTime(transport);
  uint8_t attempt = _attempt++;
  if (attempt < 4) { attempt = 4; _attempt = 5; }
  result = transport.direct(key, timestamp, attempt, text);
  if (trace) {
    char line[132];
    snprintf(line, sizeof line, "TX DM ts=%lu att=%u r=%d ack=%lu h=%08lX",
             static_cast<unsigned long>(timestamp), static_cast<unsigned>(attempt), result.code,
             static_cast<unsigned long>(result.ack),
             static_cast<unsigned long>(result.has_hash ? result.hash : 0UL));
    trace(line);
  }
  return result;
}
}
