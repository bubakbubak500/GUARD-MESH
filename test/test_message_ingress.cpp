// SPDX-License-Identifier: GPL-3.0-or-later
#undef NDEBUG
#include <cassert>

#include "ui-touch/application/MessageIngress.h"

#include <cstring>
#include <string>

using ui::MessageIngress;
using ui::MessageTypes;

namespace {

bool prepare(const UIMessageEvent &event, MessageIngress::Prepared &out,
             MessageIngress::Host host = {}) {
  return MessageIngress::prepare(event, host, out);
}

void ownedValues() {
  UIMessageEvent input(UIEventType::channelMessage, 0, nullptr,
                       "#general", "Alice: hello", 12);
  MessageIngress::Prepared first{};
  assert(prepare(input, first));
  assert(first.channel() && !first.room());
  assert(std::strcmp(first.thread(), "#general") == 0);
  assert(std::strcmp(first.sender, "Alice") == 0);
  assert(std::strcmp(first.body(), "hello") == 0);
  std::strcpy(input.text, "changed");
  std::strcpy(input.name, "different");
  assert(std::strcmp(first.body(), "hello") == 0);
  assert(std::strcmp(first.thread(), "#general") == 0);
  MessageIngress::Prepared second = first;
  std::strcpy(first.event.text, "Alice: edited");
  assert(std::strcmp(second.body(), "hello") == 0);
  assert(second.body() == second.event.text + second.bodyOffset);
  assert(first.body() == first.event.text + first.bodyOffset);
}

struct FilterContext {
  UIMessageEvent *input = nullptr;
  int pubCalls = 0;
  int nameCalls = 0;
  bool rejectPub = false;
  bool rejectName = false;
  std::string pubSeen;
  std::string nameSeen;
  MessageIngress::Prepared nested{};
};

bool pubFilter(void *raw, const uint8_t *pub) {
  FilterContext &ctx = *static_cast<FilterContext *>(raw);
  ++ctx.pubCalls;
  ctx.pubSeen.assign(reinterpret_cast<const char *>(pub), 32);
  if (ctx.input) {
    std::strcpy(ctx.input->name, "mutated");
    std::strcpy(ctx.input->text, "mutated");
    ctx.input->pub[0] = 99;
    UIMessageEvent nested(UIEventType::contactMessage, 0, nullptr, "Nested", "nested text", 2);
    assert(prepare(nested, ctx.nested));
  }
  return ctx.rejectPub;
}

bool nameFilter(void *raw, const char *name) {
  FilterContext &ctx = *static_cast<FilterContext *>(raw);
  ++ctx.nameCalls;
  ctx.nameSeen = name;
  return ctx.rejectName;
}

void callbacksAndFilters() {
  uint8_t key[32]{};
  key[0] = 7;
  UIMessageEvent input(UIEventType::channelMessage, 0, key,
                       "#room", "Alice: original", 8);
  FilterContext ctx{};
  ctx.input = &input;
  MessageIngress::Host host{};
  host.context = &ctx;
  host.ignoredPub = pubFilter;
  host.ignoredName = nameFilter;
  MessageIngress::Prepared out{};
  assert(prepare(input, out, host));
  assert(ctx.pubCalls == 1 && ctx.nameCalls == 1);
  assert(static_cast<uint8_t>(ctx.pubSeen[0]) == 7);
  assert(ctx.nameSeen == "Alice");
  assert(std::strcmp(out.thread(), "#room") == 0);
  assert(std::strcmp(out.body(), "original") == 0);
  assert(out.event.pub[0] == 7);
  assert(std::strcmp(ctx.nested.body(), "nested text") == 0);

  input = UIMessageEvent(UIEventType::channelMessage, 0, key,
                         "#room", "Alice: original", 8);
  ctx.input = nullptr;
  ctx.rejectPub = true;
  const int nameBefore = ctx.nameCalls;
  assert(!prepare(input, out, host));
  assert(ctx.nameCalls == nameBefore);
  assert(std::strcmp(out.body(), "original") == 0);
  ctx.rejectPub = false;
  ctx.rejectName = true;
  assert(!prepare(input, out, host));
  assert(ctx.nameCalls == nameBefore + 1);

  UIMessageEvent dm(UIEventType::contactMessage, 0, nullptr,
                    "Alice", "hello", 1);
  const int pubBefore = ctx.pubCalls;
  assert(prepare(dm, out, host));
  assert(!out.channel() && !out.room());
  assert(ctx.pubCalls == pubBefore);
  assert(ctx.nameCalls == nameBefore + 1);
  UIMessageEvent room(UIEventType::roomMessage, 0, nullptr,
                      "Lounge", "hello", 1, "Alice");
  assert(!prepare(room, out, host));
  assert(ctx.nameSeen == "Alice");
}

void namesAndKinds() {
  MessageIngress::Prepared out{};
  UIMessageEvent event(UIEventType::channelMessage, 0, nullptr,
                       nullptr, "Bob: hi", 0);
  assert(prepare(event, out));
  assert(std::strcmp(out.thread(), "#unknown") == 0);
  assert(std::strcmp(out.sender, "Bob") == 0);
  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         nullptr, "hello", 0);
  assert(prepare(event, out));
  assert(std::strcmp(out.thread(), "Unknown") == 0);
  assert(std::strcmp(out.sender, "node") == 0);
  event.kind = UIEventType::roomMessage;
  assert(prepare(event, out));
  assert(out.room() && !out.channel());
  assert(std::strcmp(out.thread(), "Unknown") == 0);
  event.kind = UIEventType::ack;
  assert(!prepare(event, out));
  event.kind = UIEventType::newContactMessage;
  assert(!prepare(event, out));
  event.kind = UIEventType::none;
  assert(!prepare(event, out));

  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         "DM", "hello", 0);
  std::memset(event.name, 'n', sizeof(event.name));
  std::memset(event.author, 'a', sizeof(event.author));
  assert(prepare(event, out));
  assert(std::strlen(out.thread()) == UIMessageEvent::MAX_NAME);
  assert(std::strlen(out.sender) == MessageTypes::MAX_SENDER_NAME);

  event = UIMessageEvent(UIEventType::channelMessage, 0, nullptr,
                         "#chat", "Embedded: body", 0, "Override");
  assert(prepare(event, out));
  assert(std::strcmp(out.sender, "Override") == 0);
  assert(std::strcmp(out.body(), "Embedded: body") == 0);
  event = UIMessageEvent(UIEventType::channelMessage, 0, nullptr,
                         "#chat", "plain body", 0);
  assert(prepare(event, out));
  assert(std::strcmp(out.sender, "#chat") == 0);

  const std::string name31(31, 'n');
  event = UIMessageEvent(UIEventType::channelMessage, 0, nullptr,
                         "#chat", (name31 + ": body").c_str(), 0);
  assert(prepare(event, out));
  assert(std::strcmp(out.sender, name31.c_str()) == 0);
  assert(std::strcmp(out.body(), "body") == 0);
  const std::string name32(32, 'n');
  event = UIMessageEvent(UIEventType::channelMessage, 0, nullptr,
                         "#chat", (name32 + ": body").c_str(), 0);
  assert(prepare(event, out));
  assert(std::strcmp(out.sender, "#chat") == 0);
  assert(std::strcmp(out.body(), (name32 + ": body").c_str()) == 0);
}

void textAndTiny() {
  MessageIngress::Prepared out{};
  const std::string longBody(200, 'x');
  UIMessageEvent event(UIEventType::contactMessage, 0, nullptr,
                       "DM", longBody.c_str(), 0);
  assert(prepare(event, out));
  assert(std::strlen(out.body()) == 200);
  const std::string maxBody(256, 'y');
  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         "DM", maxBody.c_str(), 0);
  assert(prepare(event, out));
  assert(std::strlen(out.body()) == UIMessageEvent::MAX_TEXT);
  event.text[UIMessageEvent::MAX_TEXT] = 'z';
  assert(prepare(event, out));
  assert(std::strlen(out.body()) == UIMessageEvent::MAX_TEXT);

  MessageIngress::Host host{};
  host.ignoreTiny = true;
  event = UIMessageEvent(UIEventType::channelMessage, 0, nullptr,
                         "#chat", "Alice: \t a \r\n", 0);
  assert(!prepare(event, out, host));
  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         "DM", " \t\r\n", 0);
  assert(!prepare(event, out, host));
  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         "DM", "ab", 0);
  assert(prepare(event, out, host));
  event = UIMessageEvent(UIEventType::contactMessage, 0, nullptr,
                         "DM", "\xc3\xa9", 0);
  assert(prepare(event, out, host));  // two UTF-8 bytes remain two bytes
}

void metadataAndTimestamp() {
  MessageIngress::Prepared out{};
  UIMessageEvent event(UIEventType::contactMessage, 4, nullptr, "DM", "hi", 0);
  event.isFlood = true;
  event.snrQ4 = 8;
  event.rssi = -50;
  event.pathBytes = 2;
  event.path[0] = 1;
  event.path[1] = 2;
  event.hasScope = true;
  event.scope = 42;
  event.scopeHome = true;
  event.scopeSlot = 5;
  assert(prepare(event, out));
  assert(out.metaFlags == 0);
  assert(out.event.pathLen == 4 && out.event.pathBytes == 0);
  assert(out.event.path[0] == 0 && out.event.snrQ4 == 0 && out.event.rssi == 0);
  assert(!out.event.hasScope && out.event.scope == 0 && out.event.scopeSlot == 0);

  event.hasRx = true;
  event.isFlood = false;
  event.pathLen = 0xff;
  assert(prepare(event, out));
  assert(out.metaFlags == (MessageTypes::MSG_META_HAS_RX |
                           MessageTypes::MSG_META_HAS_SCOPE |
                           MessageTypes::MSG_META_SCOPE_HOME | 0x50));
  assert(out.event.pathLen == 0xff && out.event.pathBytes == 0);
  assert(out.event.path[0] == 0);
  assert(out.event.scope == 42);

  event.isFlood = true;
  event.pathLen = 4;
  event.pathBytes = 255;
  event.scopeSlot = 0xff;
  event.senderTimestamp = 1700000001U;
  assert(prepare(event, out));
  assert(out.event.pathBytes == UIMessageEvent::PATH_CAPACITY);
  assert((out.metaFlags & MessageTypes::MSG_META_IS_FLOOD) != 0);
  assert(MessageTypes::metaScopeSlot(out.metaFlags) == 15);
  assert(out.event.scopeSlot == 15);
  assert(out.event.senderTimestamp == 1700000001U);

  event.hasScope = false;
  assert(prepare(event, out));
  assert(out.metaFlags == (MessageTypes::MSG_META_HAS_RX | MessageTypes::MSG_META_IS_FLOOD));
  assert(out.event.scope == 0 && !out.event.scopeHome && out.event.scopeSlot == 0);
  event.senderTimestamp = 1700000000U;
  assert(prepare(event, out) && out.event.senderTimestamp == 0);
  event.senderTimestamp = 2000000000U;
  assert(prepare(event, out) && out.event.senderTimestamp == 0);
  event.senderTimestamp = 1999999999U;
  assert(prepare(event, out) && out.event.senderTimestamp == 1999999999U);
}

}  // namespace

int main() {
  ownedValues();
  callbacksAndFilters();
  namesAndKinds();
  textAndTiny();
  metadataAndTimestamp();
}
