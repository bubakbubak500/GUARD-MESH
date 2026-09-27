// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/application/MessageIngress.h"

#include "ui-touch/ChannelSenderSplit.h"

#include <string.h>

namespace ui {
namespace {

bool trimSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool tinyBody(const char *body) {
  while (trimSpace(*body)) ++body;
  size_t length = strlen(body);
  while (length && trimSpace(body[length - 1])) --length;
  return length <= 1;
}

void copySender(char *out, size_t capacity, const char *source) {
  strncpy(out, source, capacity - 1);
  out[capacity - 1] = '\0';
}

}  // namespace

const char *MessageIngress::Prepared::thread() const {
  if (event.name[0]) return event.name;
  return channel() ? "#unknown" : "Unknown";
}

const char *MessageIngress::Prepared::body() const {
  return event.text + (bodyOffset <= UIMessageEvent::MAX_TEXT ? bodyOffset : 0);
}

bool MessageIngress::prepare(const UIMessageEvent &input, const Host &host,
                             Prepared &output) {
  // Snapshot before the first callback. Commit only after all filters pass.
  Prepared prepared{};
  prepared.event = input;
  UIMessageEvent &event = prepared.event;
  if (event.kind != UIEventType::contactMessage &&
      event.kind != UIEventType::channelMessage &&
      event.kind != UIEventType::roomMessage) return false;

  event.name[UIMessageEvent::MAX_NAME] = '\0';
  event.author[UIMessageEvent::MAX_NAME] = '\0';
  event.text[UIMessageEvent::MAX_TEXT] = '\0';
  if (event.senderTimestamp <= 1700000000U || event.senderTimestamp >= 2000000000U)
    event.senderTimestamp = 0;

  if (!event.hasRx) {
    // Legacy newMsg callers still supply the hop indicator without RF metadata.
    // It is used by glance titles even when the Info popup has no SNR/RSSI.
    event.isFlood = false;
    event.snrQ4 = 0;
    event.rssi = 0;
    event.pathBytes = 0;
    memset(event.path, 0, sizeof(event.path));
    event.hasScope = false;
  } else {
    prepared.metaFlags |= MSG_META_HAS_RX;
    if (event.isFlood) {
      prepared.metaFlags |= MSG_META_IS_FLOOD;
      if (event.pathBytes > UIMessageEvent::PATH_CAPACITY)
        event.pathBytes = UIMessageEvent::PATH_CAPACITY;
      memset(event.path + event.pathBytes, 0, sizeof(event.path) - event.pathBytes);
    } else {
      event.pathBytes = 0;
      memset(event.path, 0, sizeof(event.path));
    }
  }

  if (event.hasRx && event.hasScope) {
    prepared.metaFlags |= MSG_META_HAS_SCOPE;
    if (event.scopeHome) prepared.metaFlags |= MSG_META_SCOPE_HOME;
    prepared.metaFlags = metaWithScopeSlot(prepared.metaFlags, event.scopeSlot);
    event.scopeSlot &= 0x0F;
  } else {
    event.hasScope = false;
    event.scope = 0;
    event.scopeHome = false;
    event.scopeSlot = 0;
  }

  const char *parsedBody = event.text;
  if (prepared.channel() && !event.author[0])
    parsedBody = ChannelSenderSplit::split(event.text, prepared.sender,
                                           sizeof(prepared.sender));
  prepared.bodyOffset = static_cast<size_t>(parsedBody - event.text);

  if (event.author[0]) copySender(prepared.sender, sizeof(prepared.sender), event.author);
  else if (!prepared.sender[0])
    copySender(prepared.sender, sizeof(prepared.sender), event.name[0] ? event.name : "node");

  if (event.hasPub && host.ignoredPub && host.ignoredPub(host.context, event.pub))
    return false;
  if ((prepared.channel() || prepared.room()) && host.ignoredName &&
      host.ignoredName(host.context, prepared.sender)) return false;
  if (host.ignoreTiny && tinyBody(prepared.body())) return false;

  output = prepared;
  return true;
}

}  // namespace ui
