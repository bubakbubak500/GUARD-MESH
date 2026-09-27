// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/MessageStore.h"
#include "ui-touch/models/ContactModel.h"
#include "ui-touch/models/BatteryModel.h"
#include "ui-touch/models/LocationModel.h"
#include "ui-touch/models/TextClipboard.h"
#include "ui-touch/services/HistoryCodec.h"
#include "ui-touch/services/HistoryFileStore.h"
#include "ui-touch/services/RadioService.h"
#include "ui-touch/services/ConfigurationService.h"
#include "ui-touch/application/UiApplication.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#include <map>
#include <string>

using Store = ui::MessageStore;
static std::vector<uint32_t> dirty_records, evicted_records;
static void dirty(uint32_t seq) { dirty_records.push_back(seq); }
static void evicted(uint32_t seq) { evicted_records.push_back(seq); }
static void* allocate(size_t bytes, bool) { return std::malloc(bytes); }
static int allocations = 0, releases = 0;
static void* failAfterRing(size_t bytes, bool) { return ++allocations == 1 ? std::malloc(bytes) : nullptr; }
static void releaseCounted(void* memory) { if (memory) ++releases; std::free(memory); }
static Store::UIMessage message(uint32_t ts, const char* text = "hello") {
  Store::UIMessage result{};
  result.ts = ts;
  std::strcpy(result.sender, "Alice");
  std::strncpy(result.text, text, sizeof result.text - 1);
  return result;
}
static void messageLifecycle() {
  Store store;
  assert(!store.ready());
  int indexes[48];
  Store::UIMessage record;
  assert(store.getUnreadTotal() == 0);
  assert(store.getCombinedInboxCount(indexes, 48) == 0);
  assert(!store.getMessageByIndex(0, record));
  assert(!store.acknowledge(42, dirty));
  assert(!store.allocate(3, failAfterRing, releaseCounted));
  assert(!store.ready());
  assert(releases == 1); // the successfully allocated ring is released on partial failure
  assert(store.allocate(3, allocate, std::free));
  assert(store.ready());
  const int dm = store.findOrCreateThread("Alice", false, 1);
  const int channel = store.findOrCreateThread("#channel", true, 1);
  assert(dm >= 0 && channel >= 0 && dm != channel);
  assert(store.getCombinedInboxCount(indexes, 48) == 1 && indexes[0] == channel);
  assert(store.getCombinedInboxCount(nullptr, 48) == 0);
  assert(store.getThreadCount(false, indexes, -1) == 0);
  assert(store.append(dm, message(10), true, false, 0, evicted, dirty));
  assert(store.getUnreadTotal() == 1);
  assert(store.threadHasMessageHistory(dm));
  assert(store.getThreadMessageIndexes(dm, indexes, 48, true) == 1);
  assert(store.deleteMessageBySlot(indexes[0], dirty));
  assert(!store.threadHasMessageHistory(dm)); // cached presence must be invalidated on deletion
  assert(store.getUnreadTotal() == 0); // no phantom unread DM after its last message is gone
  assert(store.getCombinedInboxCount(indexes, 48) == 1);
  assert(store.append(channel, message(11), true, true, 0, evicted, dirty));
  assert(store.getUnreadMentionCount() == 1);
  assert(store.markAllThreadsRead());
  assert(store.getUnreadTotal() == 0 && store.getUnreadMentionCount() == 0);
  assert(!store.markAllThreadsRead());
  auto outgoing = message(12, "outgoing");
  outgoing.outgoing = true;
  outgoing.ack_hash = 42;
  outgoing.deliv_state = Store::DELIV_SENT;
  assert(store.append(dm, outgoing, false, false, 0, evicted, dirty));
  assert(store.acknowledge(42, dirty));
  assert(!store.acknowledge(42, dirty));
  assert(store.getThreadMessageIndexes(dm, indexes, 48, true) == 1);
  assert(store.getMessageByIndex(indexes[0], record) && record.deliv_state == Store::DELIV_DELIVERED);
  assert(store.append(dm, message(13), true, false, 1, evicted, dirty));
  assert(evicted_records.size() == 1 && evicted_records[0] == 1);
  assert(store.getThreadMessageIndexes(dm, indexes, 48, true) == 1);
  assert(store.getMessageByIndex(indexes[0], record) && record.seq == 4);
  assert(!store.acknowledge(42, dirty)); // tombstones cannot match a later ACK
  assert(store.clearThreadHistory(dm, dirty) == 1);
  assert(store.getUnreadTotal() == 0 && !store.threadHasMessageHistory(dm));
  assert(store.clearThreadHistory(dm, dirty) == 0);
  assert(!dirty_records.empty());
}
static void ringOrderAndCapacity() {
  Store store;
  assert(store.allocate(3, allocate, std::free));
  int thread = store.findOrCreateThread("same", false, 1);
  int group = store.findOrCreateThread("same", true, 1);
  assert(thread != group);
  for (int i = 0; i < 5; ++i) assert(store.append(thread, message(i), false, false, 0, nullptr, nullptr));
  int indexes[8];
  assert(store.getThreadMessageIndexes(thread, indexes, 8, false) == 3);
  for (int i = 0; i < 3; ++i) {
    Store::UIMessage record;
    assert(store.getMessageByIndex(indexes[i], record));
    assert(record.seq == uint32_t(i + 3) && record.ts == uint32_t(i + 2));
  }
  assert(store.getThreadMessageIndexes(group, indexes, 8, true) == 0);
  for (int i = 2; i < Store::MAX_UI_THREADS; ++i) {
    char name[16]; std::snprintf(name, sizeof name, "thread %d", i);
    assert(store.findOrCreateThread(name, false, 1) >= 0);
  }
  assert(store.findOrCreateThread("full", false, 1) == -1);
  assert(store.findOrCreateThread("same", false, 2) == thread);
}

static void messageOwnershipBoundary() {
  Store empty;
  Store::UIMessage out{};
  assert(!empty.thread(-1).used && empty.thread(0).mesh_contact_idx == -1);
  assert(!empty.restoreMessage(0, out) && !empty.removeThread(0, dirty));
  Store store; assert(store.allocate(4, allocate, std::free));
  const int alice = store.findOrCreateThread("Alice", false, 1);
  const int channel = store.findOrCreateThread("Alice", true, 1);
  const int draft = store.findOrCreateThread("Draft", false, 1);
  const int unused = store.findOrCreateThread("Unused", false, 1);
  uint8_t pub[32]{}; pub[0] = 1; pub[31] = 2;
  assert(store.bindContact(alice, 5, pub) && !store.bindContact(channel, 5, pub));
  assert(store.bindChannel(channel, 2) && !store.bindChannel(alice, 2));
  assert(store.append(alice, message(100, "older clock"), true, false, 0, nullptr, dirty));
  assert(store.append(alice, message(10, "newer arrival"), true, false, 0, nullptr, dirty));
  assert(store.append(channel, message(20, "channel"), true, true, 0, nullptr, dirty));
  assert(store.lastThreadMessage(alice, out) && !strcmp(out.text, "newer arrival"));
  assert(store.historyAt("Alice", 0) == 2 && store.historyAt("Alice", -1) < 0);
  assert(store.threadAtOrdinal(-1) < 0 && store.threadAtOrdinal(3) == unused);
  store.discardEmptyDirectThreads(draft);
  assert(store.thread(draft).used && !store.thread(unused).used && store.thread(channel).used);
  store.clearContactIndexes();
  assert(store.thread(alice).mesh_contact_idx == -1 && !memcmp(store.thread(alice).mesh_contact_pub, pub, 32));
  uint8_t collision[32]; memcpy(collision, pub, 32); collision[31] = 3;
  assert(store.reconcileContact(6, "Alice", collision, dirty) == Store::ContactChange::None);
  assert(!memcmp(store.thread(alice).mesh_contact_pub, pub, 32) && store.thread(alice).mesh_contact_idx == -1);
  dirty_records.clear();
  assert(store.reconcileContact(9, "Alicia", pub, dirty) == Store::ContactChange::Renamed);
  store.clearContactIndexes();
  assert(store.reconcileContact(9, "Alicia", pub, dirty) == Store::ContactChange::None);
  assert(store.thread(alice).mesh_contact_idx == 9 && !strcmp(store.thread(alice).name, "Alicia"));
  assert(store.threadHasMessageHistory(alice) && store.thread(alice).unread == 2);
  assert(dirty_records.size() == 2 && store.lastThreadMessage(alice, out) && !strcmp(out.thread, "Alicia"));
  assert(store.getMessageByIndex(2, out) && !strcmp(out.thread, "Alice") && out.channel);
  assert(!store.renameThread(alice, "Draft", dirty));
  assert(!strcmp(store.thread(alice).name, "Alicia"));
  assert(store.setMessageTimestamp(1, 2, 123, dirty));
  assert(!store.setMessageTimestamp(1, 1, 999, dirty));
  assert(!store.setMessageTimestamp(3, 2, 999, dirty));
  assert(!store.getMessageByIndex(3, out));
  assert(!store.restoreMessage(1, out)); // import cannot bypass live-ring mutation rules
  int purged = -1;
  assert(store.removeThread(alice, dirty, &purged) && purged == 2);
  assert(!store.thread(alice).used && !store.threadHasMessageHistory(alice));
  assert(store.lastThreadMessage(channel, out) && !strcmp(out.text, "channel"));
  assert(!store.removeThread(alice, dirty, &purged) && purged == 0);
  Store restored; assert(restored.allocate(3, allocate, std::free));
  int thread = restored.findOrCreateThread("restore", false, 1);
  restored.resetMessages();
  for (int i = 0; i < 8; ++i) {
    auto record = message(i); strcpy(record.thread, "restore"); record.seq = i + 1;
    assert(restored.restoreMessage(i % 3, record));
  }
  assert(!restored.getMessageByIndex(0, out)); // staged records are not published
  assert(!restored.restoreRingState(4, 0) && !restored.restoreRingState(3, 3));
  assert(restored.finishChronologicalRestore(8, 9));
  int indexes[4]; assert(restored.getThreadMessageIndexes(thread, indexes, 4, false) == 3);
  for (int i = 0; i < 3; ++i) {
    assert(restored.getMessageByIndex(indexes[i], out) && out.seq == unsigned(i + 6));
  }
  assert(restored.append(thread, message(9), false, false, 0, nullptr, nullptr));
  assert(restored.lastThreadMessage(thread, out) && out.seq == 9);
  restored.resequenceMessages(); assert(restored.latestSequence() == 3);
  restored.resetMessages(); assert(restored.latestSequence() == 3);
  memset(&out, 0xff, sizeof out); out.channel = false; out.outgoing = false;
  assert(restored.restoreMessage(0, out));
  assert(restored.restoreRingState(1, 1, 0)); // max uint32 sequence is exhausted, never reuse 0/1
  assert(restored.latestSequence() == UINT32_MAX && restored.getMessageByIndex(0, out));
  assert(!out.thread[Store::MAX_THREAD_NAME] && !out.sender[Store::MAX_SENDER_NAME] &&
         !out.text[Store::MAX_MSG_TEXT] && out.in_path_n == Store::MAX_UI_PATH);
  assert(!restored.append(thread, message(10), false, false, 0, nullptr, nullptr));
  std::puts("Message ownership: readonly queries, binding/rename/delete, staged ring restore and sequence identity passed.");
}
static void historyCompatibility() {
  using namespace ui::history;
  auto source = message(0x12345678, "History body");
  std::strcpy(source.thread, "#test");
  std::strcpy(source.sender, "1234567890123456789012345678901");
  source.seq = 0x87654321;
  source.meta_flags = 0x9f;
  source.path_len = 3;
  source.snr_q4 = -12;
  source.rssi = -80;
  source.ack_hash = 55; // RAM-only fields remain RAM-only in the frozen format
  source.sent_fp = 99;
  UiSegMsg encoded{};
  encode(source, &encoded);
  static_assert(sizeof encoded == 240, "Existing extended segment size");
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&encoded);
  assert(bytes[0] == 0x78 && bytes[1] == 0x56);
  assert(bytes[229] == 0x21 && bytes[232] == 0x87);
  assert(!std::memcmp(bytes + 233, "5678901", 7));
  Store::UIMessage decoded{};
  decode(encoded, &decoded);
  assert(!std::strcmp(decoded.sender, source.sender));
  assert(!std::strcmp(decoded.text, source.text));
  assert(decoded.seq == source.seq && decoded.meta_flags == source.meta_flags);
  assert(decoded.ack_hash == 0 && decoded.sent_fp == 0);
  // A pre-extension 233-byte segment gets its absent tail zero-filled by the reader.
  UiSegMsg old{}; std::memcpy(&old, &encoded, 233);
  decode(old, &decoded);
  assert(std::strlen(decoded.sender) == 24);
  assert(!std::strcmp(decoded.text, source.text) && decoded.seq == source.seq);
  decodeLegacy(encoded.m, &decoded);
  assert(std::strlen(decoded.sender) == 24 && decoded.seq == 0);
}
static void contactOrdering() {
  ui::ContactEntry entries[4]{};
  for (int i = 0; i < 4; ++i) { entries[i].mesh_idx = i; entries[i].last_heard = 900; }
  std::strcpy(entries[0].name, "Zulu"); entries[0].is_fav = true;
  std::strcpy(entries[1].name, "Beta"); entries[1].last_heard = 0;
  std::strcpy(entries[2].name, "alpha"); entries[2].gps_lat = 2000000;
  std::strcpy(entries[3].name, "Gamma"); entries[3].gps_lat = 1000000;
  ui::sortContacts(entries, 4, ui::ContactSort::Name, false, 1000, 0, 0);
  assert(entries[0].mesh_idx == 0 && entries[1].mesh_idx == 2 && entries[3].mesh_idx == 1);
  ui::sortContacts(entries, 4, ui::ContactSort::Name, true, 1000, 0, 0);
  assert(entries[0].mesh_idx == 0 && entries[1].mesh_idx == 3 && entries[3].mesh_idx == 1);
  ui::sortContacts(entries, 4, ui::ContactSort::Distance, false, 1000, 0, 0);
  assert(entries[0].is_fav);
  assert(entries[2].mesh_idx == 3 && entries[3].mesh_idx == 2);
  assert(ui::contactMatches(0, "Alpha", false, false, false, false, 0, "alp"));
  assert(!ui::contactMatches(1, "Alpha", false, false, false, false, 0, nullptr));
  assert(!ui::contactMatches(3, "Alpha", false, false, false, false, 2, nullptr));
  assert(ui::contactMatches(3, "Alpha", false, false, false, false, 0, nullptr));
  assert(!ui::contactMatches(4, "Alpha", false, false, false, false, 0, nullptr));
  assert(ui::contactMatches(5, "Alpha", false, false, true, false, 0, nullptr));
}
static void sensorPolicies() {
  ui::BatteryModel battery;
  assert(battery.percent(0) == -1 && battery.percent(3300) == 0);
  assert(battery.percent(3750) == 50 && battery.percent(4300) == 100);
  assert(!battery.isCharging(4249) && battery.isCharging(4250));
  assert(battery.sample(4000, true) == 4000 && battery.sample(4010, true) == 4002);
  assert(battery.sample(4300, true) == 4300);
  assert(battery.publish(4000, 0xfffffff0u) == 4000);
  assert(battery.publish(3990, 20) == 4000);
  assert(battery.publish(3990, 21000) == 3990);
  assert(battery.publish(4300, 21001) == 4300);
  battery.setFullMv(4100);
  assert(battery.percent(3700) == 50 && battery.isCharging(4150));
  ui::LocationModel location;
  assert(!location.shouldPersist(0, 0, 0));
  assert(location.shouldPersist(50, 14, 0xfffffff0u));
  assert(location.hadFix());
  assert(!location.shouldPersist(51, 14, 100));
  assert(location.shouldPersist(51, 14, 120000));
  location.setBaseline(52, 15);
  assert(!location.shouldPersist(52, 15, 250000));
  assert(!location.shouldPersist(std::numeric_limits<double>::quiet_NaN(), 1, 250000));
  assert(ui::distanceKm(50, 14, 50, 14) == 0);
  assert(std::fabs(ui::distanceKm(0, 0, 0, 180) - 20015.0868) < 0.01);
}
static bool popup = false, progress = false, drawer = false;
static void closePopup() { popup = false; }
static void closeDrawer() { drawer = false; }
static void applicationPolicies() {
  using App = ui::UiApplication;
  App app;
  const App::Popup entries[] = {
    {[]{ return progress; }, nullptr, App::Count},
    {[]{ return popup; }, closePopup, App::Count | App::BlockSwipe},
    {[]{ return drawer; }, closeDrawer, App::Count | App::BasePage | App::Drawer}
  };
  app.configurePopups(entries, 3);
  popup = drawer = progress = true;
  assert(app.dismissTop() == App::Dismiss::Blocked && popup && drawer);
  progress = false;
  assert(app.blocksSwipe() && app.anyPopup(true));
  assert(app.dismissTop(true) == App::Dismiss::Closed && !popup && drawer);
  assert(!app.anyPopup(true));
  assert(app.dismissTop(true) == App::Dismiss::None && drawer);
  assert(app.dismissTop() == App::Dismiss::Closed && !drawer);
  char title[] = "owned title";
  app.beginPage(title, closePopup);
  title[0] = 'x';
  assert(!std::strcmp(app.pageTitle(), "owned title"));
  assert(!app.endPage(closeDrawer) && app.pageTitle());
  assert(app.collapsePage(closePopup) && app.pageSlim() && !*app.pageTitle());
  assert(app.endPage(closePopup) && !app.pageTitle());
  const App::Popup pageEntries[] = {
    {[]{ return drawer; }, closeDrawer, App::Count | App::StatusPage},
    {[]{ return popup; }, closePopup, App::Count}
  };
  app.configurePopups(pageEntries, 2);
  drawer = true;
  assert(app.anyPopup() && !app.anyPopup(false, App::StatusPage));
  popup = true;
  assert(app.anyPopup(false, App::StatusPage)); // real dialog above a page still blocks Back
  drawer = popup = false;
  std::thread first([&]{ for (int i=0; i<10000; ++i) app.post(App::ContactsChanged); });
  std::thread second([&]{ for (int i=0; i<10000; ++i) app.post(App::WebMessagesChanged); });
  first.join(); second.join();
  assert(app.consume(App::ContactsChanged) && app.consume(App::WebMessagesChanged));
  assert(!app.pending(App::ContactsChanged) && !app.consume(App::WebMessagesChanged));
}
static bool save_ok = true;
static int saves = 0, applies = 0;
namespace ui { namespace platform {
bool savePreferences() { ++saves; return save_ok; }
void applyRadioPreferences() { ++applies; }
int maxTransmitPower() { return 22; }
} }
static void configurationPolicies() {
  NodePrefs prefs{};
  ui::ConfigurationService config(&prefs);
  assert(config.setNodeName("Node"));
  save_ok = false;
  assert(!config.setNodeName("Name write failure"));
  save_ok = true;
  assert(config.setRadioParams(869.618f, 62.5f, 8, 5, 100, 9));
  assert(prefs.tx_power_dbm == 22 && applies == 1);
  config.setPathHashMode(255); assert(prefs.path_hash_mode == 2);
  config.setTelemetryAllow(true); assert(prefs.telemetry_mode_loc == TELEM_MODE_DENY);
  config.setTelemetryAllow(false);
  config.setLocationTelemetryMode(TELEM_MODE_ALLOW_FLAGS);
  assert(prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL);
  config.setLocationTelemetryMode(255); assert(prefs.telemetry_mode_loc == TELEM_MODE_DENY);
  const int previous = saves;
  ui::ConfigurationService absent(nullptr);
  absent.setAutoAddConfig(0, 0, 0);
  assert(!absent.setNodeName("No prefs") && saves == previous);
}
struct TestRadio : ui::RadioTransport {
  uint32_t time = 100, sent_at = 0;
  uint8_t sent_attempt = 0;
  int transmissions = 0;
  bool valid_channel = true, valid_contact = true, fail = false;
  uint32_t uniqueTime() override { return time; }
  bool channelMatches(int slot, const char* name) override { return valid_channel && slot == 2 && !strcmp(name, "group"); }
  bool contactPath(const uint8_t* key, int& path) override { path = 3; return valid_contact && key[0] == 7; }
  ui::RadioSendResult result() {
    ++transmissions;
    ui::RadioSendResult r;
    r.status = fail ? ui::RadioSendResult::Failed : ui::RadioSendResult::Sent;
    r.ack = 42; r.fingerprint = 77;
    return r;
  }
  ui::RadioSendResult channel(int, const char*, uint32_t ts, const char*, const char*) override { sent_at = ts; return result(); }
  ui::RadioSendResult direct(const uint8_t*, uint32_t ts, uint8_t attempt, const char*) override {
    sent_at = ts; sent_attempt = attempt; return result();
  }
};
static void radioPolicies() {
  ui::RadioService radio;
  TestRadio transport;
  uint8_t key[32] = {7};
  using Result = ui::RadioSendResult;
  assert(radio.sendChannel(transport, 2, "wrong group", "me", "text").status == Result::MissingChannel);
  assert(transport.transmissions == 0);
  assert(radio.sendChannel(transport, 2, "group", "me", "text").fingerprint == 77);
  assert(transport.sent_at == 100);
  transport.time = 90; // clock rollback must not duplicate a send timestamp
  assert(radio.sendDirect(transport, key, "text").ack == 42);
  assert(transport.sent_at == 101 && transport.sent_attempt == 4);
  transport.fail = true;
  assert(radio.sendDirect(transport, key, "text").status == Result::Failed);
  transport.valid_contact = false;
  const int before = transport.transmissions;
  assert(radio.sendDirect(transport, key, "text").status == Result::MissingContact);
  assert(radio.sendDirect(transport, nullptr, "text").status == Result::MissingContact);
  assert(radio.sendDirect(transport, key, "").status == Result::Failed);
  assert(transport.transmissions == before);
  transport.valid_contact = true;
  for (int i = 0; i < 300; ++i) {
    radio.sendDirect(transport, key, "text");
    assert(transport.sent_attempt >= 4);
  }
  ui::RadioService independent;
  independent.sendDirect(transport, key, "text");
  assert(transport.sent_at == 90 && transport.sent_attempt == 4);
}
struct TestFile {
  std::string bytes;
  size_t pos = 0;
  bool valid = false, reject_seek = false;
  explicit operator bool() const { return valid; }
  size_t readBytes(char* out, size_t size) {
    size_t n = std::min(size, bytes.size() - pos);
    memcpy(out, bytes.data() + pos, n); pos += n; return n;
  }
  bool seek(size_t target) { if (reject_seek || target > bytes.size()) return false; pos = target; return true; }
  size_t position() const { return pos; }
};
struct TestFs {
  std::map<std::string, std::string> files;
  bool fail_rename = false;
  int removes = 0;
  TestFile open(const char* path, const char*) {
    TestFile file;
    auto found = files.find(path);
    if (found != files.end()) { file.bytes = found->second; file.valid = true; }
    return file;
  }
  bool exists(const char* path) { return files.count(path) != 0; }
  void remove(const char* path) { ++removes; files.erase(path); }
  bool rename(const char* source, const char* dest) {
    if (fail_rename || !exists(source)) return false;
    files[dest] = files[source]; files.erase(source); return true;
  }
  void mkdir(const char*) {}
};
static void historyFileFailures() {
  TestFs fs;
  ui::history::FileStore<TestFs> store(fs, "/meshcomod");
  fs.files["/meshcomod/final"] = "existing";
  assert(!store.replace("/final", "/missing") && fs.removes == 0);
  fs.files["/meshcomod/tmp"] = "new";
  assert(store.replace("/final", "/tmp") && store.open("/final", "r").bytes == "new");
  fs.files["/meshcomod/tmp"] = "keep if rename fails";
  fs.fail_rename = true;
  assert(!store.replace("/final", "/tmp") && fs.exists("/meshcomod/tmp"));
  assert(!store.open(std::string(90, 'x').c_str(), "r"));
  TestFile file; file.bytes = "abcdefgh"; file.valid = true;
  char record[6];
  assert(ui::history::readRecord(file, record, sizeof record, 3));
  assert(!memcmp(record, "abc\0\0\0", 6));
  file.pos = 0;
  assert(ui::history::readRecord(file, record, sizeof record, 8) && file.pos == 8);
  file.pos = 0; file.reject_seek = true;
  assert(!ui::history::readRecord(file, record, sizeof record, 8));
  file.pos = 7;
  assert(!ui::history::readRecord(file, record, sizeof record, 6));
  assert(!ui::history::readRecord(file, record, sizeof record, 0));
}
#include "ui-touch/services/HistoryWorkerState.h"
static void historyWorkerOwnership() {
  using ui::history::HistoryWorkerState;
  HistoryWorkerState state;
  assert(!state.claim() && !state.cancel());
  assert(state.queue() && state.pending() && state.active());
  assert(!state.queue() && state.claim() && state.running());
  assert(!state.cancel() && !state.queue());
  state.finish();
  assert(!state.active());
  for (int i = 0; i < 250; ++i) {
    assert(state.queue());
    bool claimed = false;
    std::thread worker([&]{ claimed = state.claim(); });
    const bool cancelled = state.cancel();
    worker.join();
    assert(claimed != cancelled);
    assert(state.active() == claimed);
    if (claimed) state.finish();
  }
  int snapshot = 0, result = 0;
  std::thread worker([&]{
    while (!state.claim()) std::this_thread::yield();
    result = snapshot + 1;
    state.finish();
  });
  snapshot = 41;
  assert(state.queue());
  while (state.active()) std::this_thread::yield();
  assert(result == 42);
  worker.join();
}
#include "ui-touch/models/MapProjection.h"
static void mapProjection() {
  for (uint8_t zoom = 1; zoom <= 19; ++zoom) {
    double x, y, lat, lon;
    ui::maps::latLonToWorldPx(50.0755, 14.4378, zoom, &x, &y);
    ui::maps::worldPxToLatLon(x, y, zoom, &lat, &lon);
    assert(std::abs(lat - 50.0755) < 1e-9 && std::abs(lon - 14.4378) < 1e-9);
    ui::maps::latLonToWorldPx(90, 180, zoom, &x, &y);
    assert(std::isfinite(x) && std::isfinite(y));
  }
  assert(ui::maps::gridRadius(240) == 1 && ui::maps::gridRadius(800) == 2);
}
void appStoreJobsRegression();
void firmwareUpdatesRegression();
static void clipboardPolicies() {
  ui::TextClipboard clipboard;
  assert(!*clipboard.text());
  assert(clipboard.set("Plain #channel ř🙂"));
  assert(!clipboard.set(nullptr));
  assert(!strcmp(clipboard.text(), "Plain #channel ř🙂"));
  assert(clipboard.set("#FF0000 Red ř# and ##tag", true));
  assert(!strcmp(clipboard.text(), "Red ř and #tag"));
  assert(clipboard.set("#", true) && !*clipboard.text());
  assert(clipboard.set("##", true) && !strcmp(clipboard.text(), "#"));
  assert(clipboard.set("Visible #FF00", true) && !strcmp(clipboard.text(), "Visible "));
  std::string longText(638, 'x');
  longText += "ř🙂";
  clipboard.set(longText.c_str());
  assert(strlen(clipboard.text()) == 638);
  longText.assign(637, 'x'); longText += "ř🙂";
  clipboard.set(longText.c_str());
  assert(strlen(clipboard.text()) == 639 && !strcmp(clipboard.text() + 637, "ř"));
  longText.assign(637, 'x'); longText += "#FFFFFF 🙂#";
  clipboard.set(longText.c_str(), true);
  assert(strlen(clipboard.text()) == 637);
  clipboard.set("same buffer ř");
  clipboard.set(clipboard.text());
  assert(!strcmp(clipboard.text(), "same buffer ř"));
  clipboard.set(clipboard.text() + 5);
  assert(!strcmp(clipboard.text(), "buffer ř"));
  clipboard.set("#abcdef Colored##hash", true);
  clipboard.set("");
  assert(!*clipboard.text());
}
void sightlineRegression();
void diagnosticsRegression();
void wifiScanRegression();
void clockTimeRegression();
void gpsStatusRegression();
void batteryHistoryRegression();
void notificationPolicyRegression();
void keyBindingsRegression();
void colorChoiceRegression();
void sdRestoreRegression();
void tileRequestsRegression();
int main() {
  tileRequestsRegression();
  sdRestoreRegression();
  colorChoiceRegression();
  keyBindingsRegression();
  clockTimeRegression();
  gpsStatusRegression();
  batteryHistoryRegression();
  notificationPolicyRegression();
  wifiScanRegression();
  sightlineRegression();
  diagnosticsRegression();
  firmwareUpdatesRegression();
  clipboardPolicies();
  appStoreJobsRegression();
  mapProjection();
  messageOwnershipBoundary();
  messageLifecycle(); ringOrderAndCapacity(); historyCompatibility(); contactOrdering();
  sensorPolicies(); applicationPolicies(); configurationPolicies();
  radioPolicies(); historyFileFailures(); historyWorkerOwnership();
  std::puts("UI models/services: messages, ring, history compatibility/failures, contacts, battery/GPS, navigation/events, configuration, radio passed.");
}
