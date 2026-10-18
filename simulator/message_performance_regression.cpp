// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/ChatTimeline.h"
#include "models/MessageStore.h"
#include "services/ClockSettings.h"
#include "widgets/ChatText.h"
#include "helpers/esp32/TouchPrefsStore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
namespace {
namespace timeline = ui::screens::timeline;
using Store = ui::MessageStore;
Store *model;
int active, messageReads;
uint8_t repeats;
char nodeName[33] = "s";
void check(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
void *allocate(size_t size, bool) { return std::malloc(size); }
void settle() {
  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    lv_timer_handler();
  }
}
Store::UIMessage record(const char *text, uint32_t ack = 0) {
  Store::UIMessage m{};
  m.ts = 1750000000;
  m.outgoing = ack != 0;
  m.ack_hash = ack;
  m.deliv_state = ack ? Store::DELIV_SENT : Store::DELIV_NONE;
  std::snprintf(m.sender, sizeof m.sender, "peer");
  std::snprintf(m.text, sizeof m.text, "%s", text);
  return m;
}
void refresh(ui::screens::ChatPanel &panel) { timeline::refreshChatDetail(panel); settle(); }
void forceOracle(ui::screens::ChatPanel &panel) {
  const auto height = timeline::snapshot().totalHeight;
  timeline::invalidateRows();
  timeline::invalidateContent(); refresh(panel);
  check(timeline::snapshot().totalHeight == height, "Incremental compact layout differs from full layout");
}
}
void runMessagePerformanceRegression() {
  settle(); // finish retired startup/test overlays before recording ownership
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  const bool compactBefore = touchPrefsGetCompactChat();
  ui::ClockSettings clock({});
  const auto clockBefore = clock.read();
  clock.setFlag(ui::ClockSettings::Flag::Hour12, false);
  touchPrefsSetCompactChat(true);
  Store store; model = &store;
  check(store.allocate(5000, allocate, std::free), "Performance message ring allocation failed");
  active = store.findOrCreateThread("peer", false, 0);
  const int other = store.findOrCreateThread("other", false, 0);
  ui::screens::ChatPanel direct{}, channel{};
  direct.msgs = lv_obj_create(lv_layer_top());
  lv_obj_set_size(direct.msgs, 308, 160);
  direct.detail_open = true;
  lv_obj_add_event_cb(direct.msgs, [](lv_event_t *event) {
    auto *panel = static_cast<ui::screens::ChatPanel *>(lv_event_get_user_data(event));
    timeline::chatVirtRemap1To1Scroll(panel); timeline::chatVirtSyncBubblePositions(panel);
    timeline::chatVirtScheduleRender(panel);
  }, LV_EVENT_SCROLL, &direct);
  timeline::Host host{};
  host.ready = [] { return model->ready(); };
  host.messageCapacity = [] { return model->capacity(); };
  host.messageAt = [](int slot, Store::UIMessage &out) { ++messageReads; return model->getMessageByIndex(slot, out); };
  host.activeMessages = [](int *out, int capacity) { return model->getThreadMessageIndexes(active, out, capacity, false); };
  host.hasActiveThread = [] { return true; };
  host.activeThreadIsChannel = [] { return false; };
  host.activeThreadName = [](char *out, size_t size) { std::snprintf(out, size, "peer"); };
  host.markActiveThreadRead = [] { model->markThreadRead(active); };
  host.repeats = [](uint32_t) -> uint8_t { return repeats; };
  host.nodeName = []() -> const char * { return nodeName; };
  host.direct = &direct; host.channel = &channel;
  host.statusHeight = [] { return 24; };
  host.activeThreadIsRoom = [] { return false; };
  host.popupClose = [](lv_obj_t **root) { lv_obj_del(*root); *root = nullptr; };
  host.updateJumpButtons = [](ui::screens::ChatPanel *) {};
  host.focusComposer = [](ui::screens::ChatPanel *) {};
  host.sanitize = [](const lv_font_t *, char *out, size_t size, const char *in) { std::snprintf(out, size, "%s", in); };
  host.mentionsMe = [](const char *) { return false; };
  host.retryMessage = [](int) {}; host.longPressMessage = [](int) {};
  host.trackScroll = [](lv_obj_t *) {};
  host.navigationGroup = []() -> lv_group_t * { return nullptr; };
  host.detachNavigation = [] { return false; };
  host.navigationDirty = [] {}; host.rebuildNavigation = [] {}; host.focusHint = [](lv_obj_t *) {};
  host.changesSince = [](uint32_t after, ui::MessageChanges::Item *out, int cap, int &count, uint32_t &current) {
    return model->changes().since(after, out, cap, count, current);
  };
  timeline::configure(host);
  lv_obj_update_layout(direct.msgs);
  // Find an actual font boundary: ACK's second checkmark must add a line.
  // This keeps the test meaningful with both English and Czech font chains.
  std::string wrapping;
  for (int length = 1; length <= Store::MAX_MSG_TEXT; ++length) {
    store.resetMessages();
    std::string text(length, 'i');
    check(store.append(active, record(text.c_str(), 42), false, false, 0, nullptr, nullptr), "Probe append failed");
    timeline::opened(0); timeline::refreshChatDetail(direct);
    const auto before = timeline::snapshot().totalHeight;
    store.acknowledge(42, nullptr); timeline::refreshChatDetail(direct);
    if (timeline::snapshot().totalHeight > before) { wrapping = text; break; }
  }
  check(!wrapping.empty(), "Could not find compact ACK wrap boundary");
  store.resetMessages();
  for (int i = 0; i < 4995; ++i) {
    const bool outgoing = i >= 1 && i <= 48;
    auto m = record(i == 1 || i == 3 ? wrapping.c_str() : "short", outgoing ? 1000 + i : 0);
    if (i == 4) m.sent_fp = 7;
    check(store.append(active, m, false, false, 0, nullptr, nullptr), "History fill failed");
  }
  store.append(other, record("unrelated", 99), false, false, 0, nullptr, nullptr);
  timeline::opened(0); refresh(direct);
  timeline::resetMetrics(); messageReads = 0;
  refresh(direct);
  auto work = timeline::metrics();
  std::printf("Compact performance: count=%d unchanged reads=%d signatures=%u rebuilds=%u\n",
              timeline::snapshot().count, messageReads, work.signatureChecks, work.layoutRebuilds);
  check(messageReads < 100 && work.signatureChecks < 80 && work.heightMeasurements == 0 && work.layoutRebuilds == 0,
        "Unchanged compact refresh scanned full history");
  const auto height = timeline::snapshot().totalHeight;
  timeline::resetMetrics(); messageReads = 0;
  store.acknowledge(1002, nullptr); refresh(direct);
  work = timeline::metrics();
  std::printf("Compact performance: same-height ACK reads=%d signatures=%u measurements=%u rebuilds=%u\n",
              messageReads, work.signatureChecks, work.heightMeasurements, work.layoutRebuilds);
  check(timeline::snapshot().totalHeight == height && work.heightMeasurements == 1 && work.layoutRebuilds == 0 &&
        work.signatureChecks < 80 && messageReads < 100, "Same-height ACK rebuilt/scanned compact history");
  timeline::resetMetrics(); store.acknowledge(99, nullptr); refresh(direct);
  check(timeline::metrics().heightMeasurements == 0 && timeline::metrics().layoutRebuilds == 0,
        "Unrelated ACK changed active chat layout");
  lv_obj_scroll_by(direct.msgs, 0, 120, LV_ANIM_OFF); settle();
  const int anchor = timeline::snapshot().firstVisible;
  check(anchor > 100, "Offscreen ACK fixture did not scroll away from its changed row");
  timeline::resetMetrics(); store.acknowledge(1001, nullptr); refresh(direct);
  check(timeline::snapshot().totalHeight > height && std::abs(timeline::snapshot().firstVisible - anchor) <= 1 &&
        timeline::metrics().heightMeasurements == 1, "Offscreen wrapping ACK lost height or viewport anchor");
  forceOracle(direct);
  // Existing row changes plus a new endpoint must bypass stale append offsets.
  store.acknowledge(1003, nullptr); store.append(active, record("appended"), false, false, 0, nullptr, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "ACK plus append reused stale compact prefix"); forceOracle(direct);
  repeats = 255; timeline::invalidateContent();
  store.append(active, record("echo append"), false, false, 0, nullptr, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "Echo plus append skipped full check"); forceOracle(direct);
  repeats = 0; timeline::invalidateContent();
  store.append(active, record("echo reset append"), false, false, 0, nullptr, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "Echo decrease plus append skipped full check"); forceOracle(direct);
  timeline::invalidateRows(); store.append(active, record("style append"), false, false, 0, nullptr, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "Style plus append reused stale heights"); forceOracle(direct);
  const auto beforeName = timeline::snapshot().totalHeight;
  std::memset(nodeName, 'W', sizeof nodeName - 1); nodeName[sizeof nodeName - 1] = 0; refresh(direct);
  check(timeline::snapshot().totalHeight > beforeName, "Local node name kept stale compact heights"); forceOracle(direct);
  for (int i = 5; i < 45; ++i) store.acknowledge(1000 + i, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "Journal overflow silently lost metadata changes"); forceOracle(direct);
  store.append(active, record("rollover"), false, false, 0, nullptr, nullptr); refresh(direct); forceOracle(direct);
  touchPrefsSetCompactChat(false); refresh(direct);
  const auto beforeDate = timeline::snapshot().totalHeight;
  Store::UIMessage m; check(store.getMessageByIndex(10, m), "Timestamp fixture missing");
  store.setMessageTimestamp(10, m.seq, m.ts + 86400, nullptr);
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1 && timeline::snapshot().totalHeight > beforeDate,
        "Offscreen timestamp failed to rebuild bubble day separators");
  // Preferences change without any message mutation or journal entry.
  store.resetMessages();
  const uint32_t midnight = (1750000000u / 86400u) * 86400u;
  auto first = record("first"); first.ts = midnight + 23 * 3600 + 1800;
  auto second = record("second"); second.ts = first.ts + 3600;
  store.append(active, first, false, false, 0, nullptr, nullptr);
  store.append(active, second, false, false, 0, nullptr, nullptr);
  touchPrefsSetTimeOffsetHours(0); clock.setZone(clock.zoneCount() - 1);
  refresh(direct);
  clock.stepOffset(1); timeline::resetMetrics(); refresh(direct);
  // Windows CRT does not interpret the custom POSIX <+01> zone syntax.
  // Check its invalidation independently, then use supported UTC/US zones
  // to verify a real change in day separators against a full remeasurement.
  check(timeline::metrics().layoutRebuilds == 1, "UTC offset retained cached layout"); forceOracle(direct);
  clock.setZone(2); refresh(direct);
  const auto utcHeight = timeline::snapshot().totalHeight;
  clock.setZone(3); timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1 && timeline::snapshot().totalHeight < utcHeight,
        "Timezone retained stale day separators"); forceOracle(direct);
  touchPrefsSetCompactChat(true); refresh(direct);
  clock.setFlag(ui::ClockSettings::Flag::Hour12, true);
  struct tm afternoon{}; afternoon.tm_hour = 13; afternoon.tm_min = 5;
  char formatted[16]; ui::widgets::fmtClockHM(formatted, sizeof formatted, &afternoon);
  check(std::strcmp(formatted, "1:05 PM") == 0, "Simulator does not exercise firmware's 12-hour formatting");
  timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1 && timeline::metrics().heightMeasurements == 2,
        "Clock format retained cached compact heights"); forceOracle(direct);
  clock.setZone(0); timeline::resetMetrics(); refresh(direct);
  check(timeline::metrics().layoutRebuilds == 1, "Timezone retained cached layout"); forceOracle(direct);
  timeline::shutdown(); lv_obj_del(direct.msgs); settle();
  touchPrefsSetTimeOffsetHours(clockBefore.offset); clock.setZone(clockBefore.zone);
  clock.setFlag(ui::ClockSettings::Flag::Hour12, clockBefore.hour12);
  touchPrefsSetCompactChat(compactBefore); std::strcpy(nodeName, "s"); repeats = 0; model = nullptr;
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Message performance regression leaked roots");
  puts("Message performance: large compact history, bounded ACK checks, offscreen wrapping/anchor, mixed append, echo/style/name, overflow, rollover, dates and clock preferences PASS");
}
