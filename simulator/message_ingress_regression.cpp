// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include "SimReceiveTask.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>

namespace {

void check(bool okay, const char* message) {
  if (!okay) throw std::runtime_error(message);
}

int findThread(SimReceiveTask& task, const char* wanted, bool channel) {
  int indexes[UITask::MAX_UI_THREADS];
  const int count = task.getThreadCount(channel, indexes, UITask::MAX_UI_THREADS);
  for (int i = 0; i < count; ++i) {
    bool actualChannel = false;
    uint16_t unread = 0;
    uint32_t timestamp = 0;
    char name[UITask::MAX_THREAD_NAME + 1]{};
    if (task.getThreadInfo(indexes[i], actualChannel, unread, timestamp, name, sizeof name) &&
        actualChannel == channel && std::strcmp(name, wanted) == 0)
      return indexes[i];
  }
  return -1;
}

UITask::UIMessage messageFor(SimReceiveTask& task, const char* thread, bool channel) {
  const int index = findThread(task, thread, channel);
  check(index >= 0, "Message ingress: expected thread missing");
  int slots[8]{};
  check(task.getThreadMessageIndexes(index, slots, 8, true) == 1,
        "Message ingress: unexpected message count in fixture thread");
  UITask::UIMessage result{};
  check(task.getMessageByIndex(slots[0], result), "Message ingress: expected message missing");
  return result;
}

struct Fixture {
  SimReceiveTask& task;
  size_t contactsSize;
  std::string draft;
  int selected;
  bool selectedChannel;
  bool ignoredPub;
  bool ignoredName;
  bool ignoredRejectedAuthor;
  bool ignoreTiny;
  uint8_t blockedPub[32];
  const char* blockedSender;

  Fixture(SimReceiveTask& target, const uint8_t pub[32], const char* sender)
      : task(target), contactsSize(the_mesh.contacts.size()), draft(target.getComposerBuffer()),
        selected(target.activeThreadIdx()), selectedChannel(target.activeThreadIsChannel()),
        ignoredPub(touchPrefsIsIgnored(pub)), ignoredName(touchPrefsIsNameIgnored(sender)),
        ignoredRejectedAuthor(touchPrefsIsNameIgnored("Rejected Author")),
        ignoreTiny(touchPrefsGetIgnoreTinyMsgs()), blockedSender(sender) {
    std::memcpy(blockedPub, pub, sizeof blockedPub);
  }

  ~Fixture() {
    task.afterNotify = nullptr;
    touchPrefsSetIgnored(blockedPub, ignoredPub);
    touchPrefsSetNameIgnored(blockedSender, ignoredName);
    touchPrefsSetNameIgnored("Rejected Author", ignoredRejectedAuthor);
    touchPrefsSetIgnoreTinyMsgs(ignoreTiny);
    for (const char* name : {"ingress-dm", "ingress-plain", "ingress-room",
                             "#ingress-nested", "#ingress-long", "ingress-blocked-pub",
                             "#ingress-blocked-name", "#ingress-blocked-tiny",
                             "ingress-rejected-room"}) {
      const bool channel = name[0] == '#';
      const int index = findThread(task, name, channel);
      if (index >= 0) task.removeThread(index);
    }
    if (selected >= 0) task.enterThread(selectedChannel, selected);
    task.composerReset();
    task.composerAppendText(draft.c_str());
    task.unlockScreen();
    the_mesh.contacts.resize(contactsSize);
  }
};

SimReceiveTask* reentrantTask = nullptr;
UIMessageEvent* mutableInput = nullptr;

void overwriteAndReceiveNested(UIEventType kind) {
  if (kind != UIEventType::contactMessage) return;
  reentrantTask->afterNotify = nullptr;
  *mutableInput = UIMessageEvent(UIEventType::roomMessage, 0, nullptr,
                                 "changed-input", "changed-input", 999, "changed-input");
  UIMessageEvent nested(UIEventType::channelMessage, 2, nullptr, "#ingress-nested",
                        "Nested: nested body", 202);
  nested.hasRx = true;
  nested.isFlood = true;
  nested.snrQ4 = 12;
  nested.rssi = -77;
  reentrantTask->receiveMessage(nested);
}

class LegacyReceiver final : public AbstractUITask {
public:
  LegacyReceiver() : AbstractUITask(nullptr, nullptr) {}
  UIMessageEvent* source = nullptr;
  UIEventType notified = UIEventType::none;
  int notifications = 0;
  int deliveries = 0;
  enum class Method { None, Plain, Meta, Room } method = Method::None;
  uint8_t pathLen = 0;
  bool flood = false;
  uint8_t pub[32]{};
  bool hasPub = false;
  std::string thread;
  std::string author;
  std::string body;
  int count = 0;
  int8_t snr = 0;
  int8_t rssi = 0;

  void msgRead(int) override {}
  void loop() override {}
  void notify(UIEventType kind) override {
    notified = kind;
    ++notifications;
    if (source) {
      *source = UIMessageEvent(UIEventType::none, 0, nullptr, "mutated", "mutated", -1);
    }
  }
  void newMsg(uint8_t hops, const char* name, const char* text, int msgcount) override {
    record(Method::Plain, hops, false, nullptr, name, nullptr, text, msgcount, 0, 0);
  }
  void newMsgFromPub(uint8_t hops, const uint8_t* key, const char* name,
                     const char* text, int msgcount) override {
    record(Method::Plain, hops, false, key, name, nullptr, text, msgcount, 0, 0);
  }
  void newMsgFromPubWithMeta(uint8_t hops, bool isFlood, const uint8_t* key,
                             const char* name, const char* text, int msgcount,
                             int8_t snrQ4, int8_t signal) override {
    record(Method::Meta, hops, isFlood, key, name, nullptr, text, msgcount, snrQ4, signal);
  }
  void newRoomMsgFromPubWithMeta(uint8_t hops, bool isFlood, const uint8_t* key,
                                 const char* name, const char* sender, const char* text,
                                 int msgcount, int8_t snrQ4, int8_t signal) override {
    record(Method::Room, hops, isFlood, key, name, sender, text, msgcount, snrQ4, signal);
  }

private:
  void record(Method which, uint8_t hops, bool isFlood, const uint8_t* key,
              const char* name, const char* sender, const char* text, int msgcount,
              int8_t snrQ4, int8_t signal) {
    ++deliveries;
    method = which;
    pathLen = hops;
    flood = isFlood;
    hasPub = key != nullptr;
    if (key) std::memcpy(pub, key, sizeof pub);
    thread = name ? name : "";
    author = sender ? sender : "";
    body = text ? text : "";
    count = msgcount;
    snr = snrQ4;
    rssi = signal;
  }
};

void checkLegacyAdapter() {
  LegacyReceiver receiver;
  uint8_t pub[32];
  std::memset(pub, 0xB4, sizeof pub);

  UIMessageEvent plain(UIEventType::contactMessage, 3, pub, "legacy-dm", "legacy plain", 41);
  receiver.source = &plain;
  receiver.receiveMessage(plain);
  check(receiver.notifications == 1 && receiver.notified == UIEventType::contactMessage &&
        receiver.deliveries == 1 && receiver.method == LegacyReceiver::Method::Plain &&
        receiver.pathLen == 3 && receiver.hasPub && std::memcmp(receiver.pub, pub, 32) == 0 &&
        receiver.thread == "legacy-dm" && receiver.body == "legacy plain" && receiver.count == 41,
        "Message ingress: legacy plain dispatch lost owned data after notify");

  UIMessageEvent meta(UIEventType::channelMessage, 4, pub, "#legacy", "legacy meta", 42);
  meta.hasRx = true;
  meta.isFlood = true;
  meta.snrQ4 = -8;
  meta.rssi = -91;
  receiver.source = &meta;
  receiver.receiveMessage(meta);
  check(receiver.notifications == 2 && receiver.notified == UIEventType::channelMessage &&
        receiver.deliveries == 2 && receiver.method == LegacyReceiver::Method::Meta &&
        receiver.pathLen == 4 && receiver.flood && receiver.hasPub &&
        std::memcmp(receiver.pub, pub, 32) == 0 && receiver.thread == "#legacy" &&
        receiver.body == "legacy meta" && receiver.count == 42 &&
        receiver.snr == -8 && receiver.rssi == -91,
        "Message ingress: legacy metadata dispatch lost owned data after notify");

  UIMessageEvent room(UIEventType::roomMessage, 5, pub, "legacy-room", "legacy room", 43, "author");
  room.hasRx = true;
  room.isFlood = false;
  room.snrQ4 = 20;
  room.rssi = -67;
  receiver.source = &room;
  receiver.receiveMessage(room);
  check(receiver.notifications == 3 && receiver.notified == UIEventType::roomMessage &&
        receiver.deliveries == 3 && receiver.method == LegacyReceiver::Method::Room &&
        receiver.pathLen == 5 && !receiver.flood && receiver.hasPub &&
        std::memcmp(receiver.pub, pub, 32) == 0 && receiver.thread == "legacy-room" &&
        receiver.author == "author" && receiver.body == "legacy room" &&
        receiver.count == 43 && receiver.snr == 20 && receiver.rssi == -67,
        "Message ingress: legacy room dispatch lost owned data after notify");

  UIMessageEvent unsupported;
  unsupported.kind = UIEventType::ack;
  receiver.source = &unsupported;
  receiver.receiveMessage(unsupported);
  check(receiver.notifications == 3 && receiver.deliveries == 3,
        "Message ingress: legacy adapter notified for a non-message event");
}

}  // namespace

void runMessageIngressIntegration(SimReceiveTask& task) {
  checkLegacyAdapter();
  uint8_t blockedPub[32];
  std::memset(blockedPub, 0xD1, sizeof blockedPub);
  const std::string blockedSender(31, 'N');
  for (const char* name : {"ingress-dm", "ingress-plain", "ingress-room",
                           "#ingress-nested", "#ingress-long", "ingress-blocked-pub",
                           "#ingress-blocked-name", "#ingress-blocked-tiny",
                           "ingress-rejected-room"})
    check(findThread(task, name, name[0] == '#') < 0,
          "Message ingress: fixture thread name already exists");
  Fixture fixture(task, blockedPub, blockedSender.c_str());

  task.unlockScreen();
  task.sleepScreen();
  check(task.isScreenOff(), "Message ingress: fixture could not sleep screen");
  const unsigned beforeFilters = task.messageNotifications;
  touchPrefsSetIgnored(blockedPub, true);
  UIMessageEvent blockedPubEvent(UIEventType::contactMessage, 0, blockedPub,
                                 "ingress-blocked-pub", "blocked pub", 101);
  task.receiveMessage(blockedPubEvent);
  check(findThread(task, "ingress-blocked-pub", false) < 0 &&
        task.messageNotifications == beforeFilters && task.isScreenOff() && task.getMsgCount() == 101,
        "Message ingress: blocked pub created a thread, notified, or woke screen");

  touchPrefsSetNameIgnored(blockedSender.c_str(), true);
  const std::string blockedText = blockedSender + ": blocked sender";
  UIMessageEvent blockedNameEvent(UIEventType::channelMessage, 0, nullptr,
                                  "#ingress-blocked-name", blockedText.c_str(), 102);
  task.receiveMessage(blockedNameEvent);
  check(findThread(task, "#ingress-blocked-name", true) < 0 &&
        task.messageNotifications == beforeFilters && task.isScreenOff() && task.getMsgCount() == 102,
        "Message ingress: blocked 31-character sender was delivered");

  touchPrefsSetIgnoreTinyMsgs(true);
  UIMessageEvent tiny(UIEventType::channelMessage, 0, nullptr,
                      "#ingress-blocked-tiny", "Short: x", 103);
  task.receiveMessage(tiny);
  check(findThread(task, "#ingress-blocked-tiny", true) < 0 &&
        task.messageNotifications == beforeFilters && task.isScreenOff() && task.getMsgCount() == 103,
        "Message ingress: tiny channel body was delivered");
  touchPrefsSetIgnoreTinyMsgs(false);
  touchPrefsSetIgnored(blockedPub, fixture.ignoredPub);
  touchPrefsSetNameIgnored(blockedSender.c_str(), fixture.ignoredName);
  task.wakeScreen();

  uint8_t firstPub[32];
  std::memset(firstPub, 0xC1, sizeof firstPub);
  UIMessageEvent first(UIEventType::contactMessage, 3, firstPub,
                       "ingress-dm", "first owned body", 201);
  first.hasRx = true;
  first.isFlood = true;
  first.snrQ4 = -12;
  first.rssi = -85;
  first.pathBytes = 3;
  first.path[0] = 0x11;
  first.path[1] = 0x22;
  first.path[2] = 0x33;
  first.hasScope = true;
  first.scope = 0xABCD;
  first.scopeHome = true;
  first.scopeSlot = 6;
  first.senderTimestamp = 1800000001U;
  const unsigned beforeReentry = task.messageNotifications;
  reentrantTask = &task;
  mutableInput = &first;
  task.afterNotify = overwriteAndReceiveNested;
  task.receiveMessage(first);
  task.afterNotify = nullptr;
  reentrantTask = nullptr;
  mutableInput = nullptr;
  check(task.messageNotifications == beforeReentry + 2,
        "Message ingress: reentrant deliveries missed notifications");
  const auto firstStored = messageFor(task, "ingress-dm", false);
  const auto nestedStored = messageFor(task, "#ingress-nested", true);
  uint8_t bound[32]{};
  check(std::strcmp(firstStored.text, "first owned body") == 0 &&
        std::strcmp(firstStored.thread, "ingress-dm") == 0 &&
        firstStored.ts == 1800000001U && firstStored.path_len == 3 &&
        firstStored.snr_q4 == -12 && firstStored.rssi == -85 &&
        firstStored.in_path_n == 3 &&
        std::memcmp(firstStored.in_path, "\x11\x22\x33", 3) == 0 &&
        firstStored.in_scope == 0xABCD &&
        (firstStored.meta_flags & (UITask::MSG_META_HAS_RX | UITask::MSG_META_IS_FLOOD |
                                    UITask::MSG_META_HAS_SCOPE | UITask::MSG_META_SCOPE_HOME)) ==
            (UITask::MSG_META_HAS_RX | UITask::MSG_META_IS_FLOOD |
             UITask::MSG_META_HAS_SCOPE | UITask::MSG_META_SCOPE_HOME) &&
        UITask::metaScopeSlot(firstStored.meta_flags) == 6 &&
        task.getThreadContactPub(findThread(task, "ingress-dm", false), bound) &&
        std::memcmp(bound, firstPub, 32) == 0,
        "Message ingress: reentrant callback changed first event or pinned identity");
  check(nestedStored.channel && std::strcmp(nestedStored.sender, "Nested") == 0 &&
        std::strcmp(nestedStored.text, "nested body") == 0 &&
        (nestedStored.meta_flags & UITask::MSG_META_HAS_RX) != 0 &&
        std::strcmp(nestedStored.thread, "#ingress-nested") == 0,
        "Message ingress: nested event entered the wrong conversation");

  UIMessageEvent plain(UIEventType::contactMessage, 7, nullptr,
                       "ingress-plain", "plain body", 203);
  plain.isFlood = true;
  plain.pathBytes = 1;
  plain.path[0] = 0x55;
  plain.hasScope = true;
  plain.scope = 0x3456;
  task.receiveMessage(plain);
  const auto plainStored = messageFor(task, "ingress-plain", false);
  check(plainStored.meta_flags == 0 && plainStored.path_len == 7 &&
        plainStored.in_path_n == 0 && plainStored.in_scope == 0,
        "Message ingress: plain delivery retained RX metadata");

  uint8_t roomPub[32];
  std::memset(roomPub, 0xC2, sizeof roomPub);
  UIMessageEvent room(UIEventType::roomMessage, 0xFF, roomPub,
                      "ingress-room", "room body", 204, "Room Author");
  room.hasRx = true;
  room.isFlood = false;
  room.snrQ4 = 4;
  room.rssi = -70;
  room.hasScope = true;
  room.scope = 0x9876;
  room.scopeSlot = 3;
  room.senderTimestamp = 1800000100U;
  task.receiveMessage(room);
  const auto roomStored = messageFor(task, "ingress-room", false);
  check(!roomStored.channel && std::strcmp(roomStored.sender, "Room Author") == 0 &&
        std::strcmp(roomStored.text, "room body") == 0 && roomStored.ts == 1800000100U &&
        roomStored.path_len == 0xFF && roomStored.in_path_n == 0 &&
        roomStored.in_scope == 0x9876 &&
        (roomStored.meta_flags & UITask::MSG_META_HAS_RX) &&
        !(roomStored.meta_flags & UITask::MSG_META_IS_FLOOD) &&
        UITask::metaScopeSlot(roomStored.meta_flags) == 3,
        "Message ingress: room replay or scoped direct metadata lost");

  touchPrefsSetNameIgnored("Rejected Author", true);
  UIMessageEvent rejectedRoom(UIEventType::roomMessage, 0, nullptr,
                              "ingress-rejected-room", "rejected body", 205, "Rejected Author");
  rejectedRoom.senderTimestamp = 1800000200U;
  task.receiveMessage(rejectedRoom);
  check(findThread(task, "ingress-rejected-room", false) < 0,
        "Message ingress: rejected room post created a thread");
  touchPrefsSetNameIgnored("Rejected Author", fixture.ignoredRejectedAuthor);

  task.notify(UIEventType::contactMessage);  // stale legacy kind must not classify the next event.
  const std::string longSender(31, 'S');
  const std::string longBody(170, 'B');
  const std::string channelText = longSender + ": " + longBody;
  UIMessageEvent channel(UIEventType::channelMessage, 1, nullptr,
                         "#ingress-long", channelText.c_str(), 206);
  channel.hasRx = true;
  channel.isFlood = true;
  const uint32_t beforeLocal = static_cast<uint32_t>(std::time(nullptr));
  task.receiveMessage(channel);
  const uint32_t afterLocal = static_cast<uint32_t>(std::time(nullptr));
  const auto channelStored = messageFor(task, "#ingress-long", true);
  check(channelStored.channel && std::strcmp(channelStored.sender, longSender.c_str()) == 0 &&
        std::strlen(channelStored.text) == UITask::MAX_MSG_TEXT &&
        std::strncmp(channelStored.text, longBody.c_str(), UITask::MAX_MSG_TEXT) == 0 &&
        channelStored.ts >= beforeLocal && channelStored.ts <= afterLocal &&
        channelStored.ts != rejectedRoom.senderTimestamp,
        "Message ingress: channel kind, full sender, body width, or local time incorrect");

  std::puts("Message ingress UITask integration: PASS");
}
