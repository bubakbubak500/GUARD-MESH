#include "ui-touch/application/ChatSession.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstring>

namespace {

void *allocate(size_t bytes, bool) { return calloc(1, bytes); }
void release(void *memory) { free(memory); }

struct Directory {
  ui::ChatSession::Contact contacts[8]{};
  int contactsUsed = 0;
  ui::ChatSession::Channel channels[8]{};
  int channelsUsed = 0;

  static int contactCount(void *context) {
    return static_cast<Directory *>(context)->contactsUsed;
  }
  static bool contact(void *context, int index, ui::ChatSession::Contact &out) {
    Directory &self = *static_cast<Directory *>(context);
    if (index < 0 || index >= self.contactsUsed) return false;
    out = self.contacts[index];
    return true;
  }
  static int channelCount(void *context) {
    return static_cast<Directory *>(context)->channelsUsed;
  }
  static bool channel(void *context, int index, ui::ChatSession::Channel &out) {
    Directory &self = *static_cast<Directory *>(context);
    if (index < 0 || index >= self.channelsUsed) return false;
    out = self.channels[index];
    return true;
  }
  ui::ChatSession::Host host() {
    ui::ChatSession::Host result{};
    result.context = this;
    result.contactCount = contactCount;
    result.contact = contact;
    result.channelCount = channelCount;
    result.channel = channel;
    return result;
  }
};

void setContact(ui::ChatSession::Contact &contact, const char *name, uint8_t seed) {
  strncpy(contact.name, name, ui::MessageTypes::MAX_THREAD_NAME);
  contact.name[ui::MessageTypes::MAX_THREAD_NAME] = '\0';
  for (size_t i = 0; i < sizeof(contact.pub); ++i)
    contact.pub[i] = static_cast<uint8_t>(seed + i);
}

void setChannel(ui::ChatSession::Channel &channel, const char *name) {
  strncpy(channel.name, name, ui::MessageTypes::MAX_THREAD_NAME);
  channel.name[ui::MessageTypes::MAX_THREAD_NAME] = '\0';
}

struct FakeTransport : ui::RadioTransport {
  Directory *directory = nullptr;
  int paths = 0, directCalls = 0, channelCalls = 0;
  uint8_t lastPub[32]{};
  int lastChannelSlot = -1;
  char lastText[ui::MessageTypes::MAX_MSG_TEXT + 1]{};
  ui::RadioSendResult nextResult{};
  void (*duringSend)(FakeTransport &) = nullptr;

  uint32_t uniqueTime() override { return 10; }
  bool channelMatches(int slot, const char *name) override {
    if (!directory || slot < 0 || slot >= directory->channelsUsed) return false;
    return strcmp(directory->channels[slot].name, name) == 0;
  }
  bool contactPath(const uint8_t *key, int &path) override {
    ++paths;
    path = 1;
    if (!directory) return false;
    for (int i = 0; i < directory->contactsUsed; ++i)
      if (memcmp(directory->contacts[i].pub, key, sizeof(lastPub)) == 0) return true;
    return false;
  }
  ui::RadioSendResult channel(int slot, const char *, uint32_t,
                              const char *, const char *text) override {
    ++channelCalls;
    lastChannelSlot = slot;
    strncpy(lastText, text, sizeof(lastText) - 1);
    if (duringSend) duringSend(*this);
    return nextResult;
  }
  ui::RadioSendResult direct(const uint8_t *key, uint32_t, uint8_t,
                             const char *text) override {
    ++directCalls;
    memcpy(lastPub, key, sizeof(lastPub));
    strncpy(lastText, text, sizeof(lastText) - 1);
    if (duringSend) duringSend(*this);
    return nextResult;
  }
};

int makeThread(ui::MessageStore &store, const char *name, bool channel = false) {
  return store.findOrCreateThread(name, channel, 1);
}

void testIdentityResolution() {
  ui::MessageStore store;
  assert(store.allocate(8, allocate, release));
  Directory directory;
  setContact(directory.contacts[0], "Other", 80);
  setContact(directory.contacts[1], "Peer", 10);
  directory.contactsUsed = 2;
  ui::ChatSession session;
  session.configure(store, directory.host());
  const int thread = makeThread(store, "Peer");
  assert(store.bindContact(thread, 0, nullptr));
  // Legacy key6 resolves to the second contact, not the stale cached index.
  ui::MessageTypes::UIThread legacy = store.thread(thread);
  memcpy(legacy.mesh_contact_key6, directory.contacts[1].pub, 6);
  legacy.mesh_contact_idx = 0;
  assert(store.restoreThread(thread, legacy));
  assert(session.select(thread, false));
  assert(store.thread(thread).mesh_contact_idx == 1);
  assert(memcmp(store.thread(thread).mesh_contact_pub, directory.contacts[1].pub, 32) == 0);
  FakeTransport transport;
  transport.directory = &directory;
  transport.nextResult.status = ui::RadioSendResult::Sent;
  const auto sent = session.send(transport, "hello", "me", 20);
  assert(sent.status == ui::ChatSession::Status::Sent);
  assert(memcmp(transport.lastPub, directory.contacts[1].pub, 32) == 0);
  // Selecting a legacy/name-only thread must pin the resolved key too.
  assert(store.bindContact(thread, 0, directory.contacts[0].pub));
  ui::ChatSession::Contact resolved{};
  assert(!session.syncBindings(thread));
  assert(!session.activeContact(resolved));
  const int beforeConflict = transport.directCalls;
  assert(session.send(transport, "blocked repin", "me", 20).status == ui::ChatSession::Status::MissingKey);
  assert(transport.directCalls == beforeConflict);
  assert(store.restoreThread(thread, legacy));
  assert(session.syncBindings(thread));
  assert(memcmp(store.thread(thread).mesh_contact_pub, directory.contacts[1].pub, 32) == 0);

  // Missing key6 must not fall back to a same-name contact.
  ui::MessageTypes::UIThread missing = store.thread(thread);
  memset(missing.mesh_contact_pub, 0, sizeof(missing.mesh_contact_pub));
  memset(missing.mesh_contact_key6, 0, sizeof(missing.mesh_contact_key6));
  memcpy(missing.mesh_contact_key6, "123456", 6);
  assert(store.restoreThread(thread, missing));
  directory.contactsUsed = 1;
  setContact(directory.contacts[0], "Peer", 80);
  assert(session.select(thread, false));
  const int oldCalls = transport.directCalls;
  assert(session.send(transport, "hello", "me", 20).status == ui::ChatSession::Status::MissingKey);
  assert(transport.directCalls == oldCalls);

  // An ambiguous prefix is unresolved even when a name candidate exists.
  setContact(directory.contacts[0], "Peer", 10);
  setContact(directory.contacts[1], "Peer", 10);
  directory.contacts[1].pub[31]++;
  directory.contactsUsed = 2;
  memcpy(missing.mesh_contact_key6, directory.contacts[0].pub, 6);
  assert(store.restoreThread(thread, missing));
  assert(session.select(thread, false));
  assert(session.send(transport, "hello", "me", 20).status == ui::ChatSession::Status::MissingKey);
  assert(transport.directCalls == oldCalls);

  // Unkeyed duplicate names are also ambiguous.
  memset(missing.mesh_contact_key6, 0, sizeof(missing.mesh_contact_key6));
  assert(store.restoreThread(thread, missing));
  assert(session.select(thread, false));
  assert(session.send(transport, "hello", "me", 20).status == ui::ChatSession::Status::MissingKey);
  assert(transport.directCalls == oldCalls);

  // A pinned full key missing from the directory is sent to RadioService as-is;
  // a same-name but different key cannot capture the message.
  ui::MessageTypes::UIThread pinned = store.thread(thread);
  for (size_t i = 0; i < sizeof(pinned.mesh_contact_pub); ++i)
    pinned.mesh_contact_pub[i] = static_cast<uint8_t>(200 + i);
  memcpy(pinned.mesh_contact_key6, pinned.mesh_contact_pub, 6);
  assert(store.restoreThread(thread, pinned));
  setContact(directory.contacts[0], "Peer", 10);
  directory.contactsUsed = 1;
  assert(session.select(thread, false));
  assert(session.send(transport, "hello", "me", 20).status == ui::ChatSession::Status::MissingContact);
  assert(transport.directCalls == oldCalls);
  assert(transport.paths > 0);
}

void testChannelsAndSelection() {
  ui::MessageStore store;
  assert(store.allocate(8, allocate, release));
  Directory directory;
  setChannel(directory.channels[0], "#other");
  setChannel(directory.channels[1], "#wanted");
  directory.channelsUsed = 2;
  ui::ChatSession session;
  session.configure(store, directory.host());
  const int channel = makeThread(store, "#wanted", true);
  assert(store.bindChannel(channel, 0));
  assert(session.select(channel, true));
  assert(store.thread(channel).mesh_channel_slot == 1);
  FakeTransport transport;
  transport.directory = &directory;
  transport.nextResult.status = ui::RadioSendResult::Sent;
  const auto oldFirstChannel = directory.channels[0];
  directory.channels[0] = directory.channels[1];
  directory.channels[1] = oldFirstChannel;
  assert(session.send(transport, "announcement", "me", 30).status == ui::ChatSession::Status::Sent);
  assert(transport.channelCalls == 1 && transport.lastChannelSlot == 0);
  directory.channels[0] = directory.channels[1];
  assert(session.send(transport, "missing", "me", 30).status == ui::ChatSession::Status::MissingChannel);
  assert(transport.channelCalls == 1);

  const int dmIndex = makeThread(store, "Peer");
  assert(dmIndex >= 0);
  assert(session.select(dmIndex, false));
  assert(!session.activeChannel());
  const uint32_t revision = session.revision();
  assert(!session.select(channel, false));
  assert(session.activeIndex() == -1 && !session.activeChannel());
  assert(session.revision() == revision + 1);
  session.select(channel, true);
  session.clear();
  assert(session.activeIndex() == -1 && !session.activeChannel());
  assert(!session.canCommit(session.send(transport, "x", "me", 1).target));
}

struct CallbackContext {
  ui::ChatSession *session = nullptr;
  int nextThread = -1;
  char *source = nullptr;
  FakeTransport *transport = nullptr;
  ui::ChatSession::SendResult nested{};
};

CallbackContext *callbackContext = nullptr;

void testSnapshotsAndCallbacks() {
  ui::MessageStore store;
  assert(store.allocate(8, allocate, release));
  Directory directory;
  setContact(directory.contacts[0], "A", 5);
  setContact(directory.contacts[1], "B", 90);
  directory.contactsUsed = 2;
  ui::ChatSession session;
  session.configure(store, directory.host());
  const int first = makeThread(store, "A");
  const int second = makeThread(store, "B");
  assert(store.bindContact(first, 0, directory.contacts[0].pub));
  assert(store.bindContact(second, 1, directory.contacts[1].pub));
  assert(session.select(first, false));
  FakeTransport transport;
  transport.directory = &directory;
  transport.nextResult.status = ui::RadioSendResult::Sent;
  char source[] = "original";
  CallbackContext ctx{};
  ctx.session = &session;
  ctx.nextThread = second;
  ctx.source = source;
  callbackContext = &ctx;
  transport.duringSend = [](FakeTransport &) {
    CallbackContext &current = *callbackContext;
    current.source[0] = 'X';
    current.session->select(current.nextThread, false);
  };
  const auto sent = session.send(transport, source, "me", 20);
  transport.duringSend = nullptr;
  assert(sent.status == ui::ChatSession::Status::Sent);
  assert(strcmp(sent.text, "original") == 0);
  assert(sent.target.thread == first);
  assert(!session.matchesSelection(sent.target));
  assert(session.canCommit(sent.target));

  // Clearing selection still leaves the completed result attributable to its
  // unchanged original thread; repinning that slot makes it unsafe to commit.
  session.clear();
  assert(session.canCommit(sent.target));
  uint8_t replacement[32];
  setContact(directory.contacts[2], "A", 170);
  memcpy(replacement, directory.contacts[2].pub, sizeof(replacement));
  assert(store.bindContact(first, -1, replacement));
  assert(!session.canCommit(sent.target));
  callbackContext = nullptr;
}

void testReceivedPinningAndExplicitRebind() {
  ui::MessageStore store;
  assert(store.allocate(8, allocate, release));
  Directory directory;
  setContact(directory.contacts[0], "Same", 1);
  setContact(directory.contacts[1], "Same", 80);
  directory.contactsUsed = 2;
  ui::ChatSession session;
  session.configure(store, directory.host());
  const int thread = makeThread(store, "Same");
  assert(store.bindContact(thread, 0, directory.contacts[0].pub));
  assert(session.select(thread, false));
  const uint32_t before = session.revision();
  assert(!session.bindReceivedContact(thread, directory.contacts[1].pub));
  assert(session.revision() == before);
  assert(memcmp(store.thread(thread).mesh_contact_pub, directory.contacts[0].pub, 32) == 0);

  // An explicit user open after a deliberate model rebind selects the new key.
  assert(store.bindContact(thread, 1, directory.contacts[1].pub));
  assert(session.select(thread, false));
  ui::ChatSession::Contact resolved{};
  assert(session.activeContact(resolved));
  assert(memcmp(resolved.pub, directory.contacts[1].pub, 32) == 0);

  assert(session.bindReceivedContact(thread, directory.contacts[1].pub));
  assert(session.revision() == before + 1); // explicit select changed session identity

  const int unkeyed = makeThread(store, "New peer");
  assert(session.select(unkeyed, false));
  const uint32_t unkeyedRevision = session.revision();
  assert(session.bindReceivedContact(unkeyed, directory.contacts[0].pub));
  assert(session.revision() == unkeyedRevision + 1);
}

void testStatusesAndBusy() {
  ui::MessageStore store;
  assert(store.allocate(8, allocate, release));
  Directory directory;
  setContact(directory.contacts[0], "Peer", 20);
  directory.contactsUsed = 1;
  ui::ChatSession session;
  session.configure(store, directory.host());
  const int thread = makeThread(store, "Peer");
  assert(store.bindContact(thread, 0, directory.contacts[0].pub));
  assert(session.select(thread, false));
  FakeTransport transport;
  transport.directory = &directory;
  assert(session.send(transport, "", "me", 2).status == ui::ChatSession::Status::Empty);
  transport.nextResult.status = ui::RadioSendResult::Failed;
  assert(session.send(transport, "x", "me", 2).status == ui::ChatSession::Status::Failed);

  CallbackContext ctx{};
  ctx.session = &session;
  ctx.transport = &transport;
  callbackContext = &ctx;
  transport.duringSend = [](FakeTransport &) {
    // The active outer send owns the guard throughout the transport callback.
    // A nested send reports Busy before attempting another transport operation.
    auto nested = callbackContext->session->send(*callbackContext->transport,
                                                "nested", "me", 5);
    callbackContext->nested = nested;
  };
  transport.nextResult.status = ui::RadioSendResult::Sent;
  assert(session.send(transport, "outer", "me", 8).status == ui::ChatSession::Status::Sent);
  assert(ctx.nested.status == ui::ChatSession::Status::Busy);
  assert(transport.directCalls == 2);
  callbackContext = nullptr;
}

} // namespace

int main() {
  testIdentityResolution();
  testChannelsAndSelection();
  testSnapshotsAndCallbacks();
  testReceivedPinningAndExplicitRebind();
  testStatusesAndBusy();
  return 0;
}
