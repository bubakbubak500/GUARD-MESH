// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/ChatTimeline.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace {
namespace timeline = ui::screens::timeline;
using ui::MessageTypes;
using ui::screens::ChatPanel;
int first = 0, count = 240, reads = 0, actions = 0;
uint32_t sequenceOffset = 0;
bool longText = false;
void check(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}
void drain() {
  for (int i = 0; i < 12; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    lv_timer_handler();
  }
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && strstr(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *result = label(lv_obj_get_child(root, i), text))
      return result;
  return nullptr;
}
void createMessages(ChatPanel &panel) {
  panel.msgs = lv_obj_create(lv_layer_top());
  lv_obj_set_size(panel.msgs, 308, 160);
  panel.detail_open = true;
}
} // namespace
void runChatTimelineRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  ChatPanel direct{}, channel{};
  createMessages(direct);
  timeline::Host host{};
  host.ready = [] { return true; };
  host.messageCapacity = [] { return 500; };
  host.messageAt = [](int index, MessageTypes::UIMessage &out) {
    if (index < 0 || index >= 500)
      return false;
    out = {};
    out.seq = index + 1 + sequenceOffset;
    out.ts = 1750000000 + index;
    strcpy(out.sender, "peer");
    snprintf(
        out.text, sizeof out.text, "%s %d https://example.com/%d",
        longText
            ? "A longer message with several additional words to change the measured row height significantly"
            : "Message",
        index, index);
    return true;
  };
  host.activeMessages = [](int *indexes, int capacity) {
    int n = count < capacity ? count : capacity;
    for (int i = 0; i < n; ++i)
      indexes[i] = first + i;
    return n;
  };
  host.hasActiveThread = [] { return true; };
  host.activeThreadIsChannel = [] { return false; };
  host.activeThreadName = [](char *text, size_t size) { snprintf(text, size, "test"); };
  host.markActiveThreadRead = [] { ++reads; };
  host.repeats = [](uint32_t) -> uint8_t { return 0; };
  host.nodeName = [] { return "self"; };
  host.direct = &direct;
  host.channel = &channel;
  host.statusHeight = [] { return 24; };
  host.activeThreadIsRoom = [] { return false; };
  host.popupClose = [](lv_obj_t **root) {
    lv_obj_del_async(*root);
    *root = nullptr;
  };
  host.updateJumpButtons = [](ChatPanel *) {};
  host.focusComposer = [](ChatPanel *) {};
  host.sanitize = [](const lv_font_t *, char *out, size_t size, const char *in) {
    snprintf(out, size, "%s", in);
  };
  host.mentionsMe = [](const char *) { return false; };
  host.retryMessage = [](int) {};
  host.longPressMessage = [](int) { ++actions; };
  host.trackScroll = [](lv_obj_t *) {};
  host.navigationGroup = []() -> lv_group_t * { return nullptr; };
  host.detachNavigation = [] { return false; };
  host.navigationDirty = [] {};
  host.rebuildNavigation = [] {};
  host.focusHint = [](lv_obj_t *) {};
  timeline::configure(host);
  timeline::opened(0);
  timeline::refreshChatDetail(direct);
  drain();
  auto state = timeline::snapshot();
  check(state.count == 240 && state.totalHeight > 7500, "Long chat did not use virtual layout");
  check(state.lastVisible == 239 && lv_obj_get_child_cnt(direct.msgs) < 40,
        "Chat failed to open at latest with bounded rows");
  timeline::chatVirtJumpToOldest(&direct);
  drain();
  check(timeline::snapshot().firstVisible == 0, "Oldest jump missed first message");
  auto *firstRow = lv_obj_get_parent(label(direct.msgs, "Message 0"));
  lv_event_send(firstRow, LV_EVENT_CLICKED, nullptr);
  lv_event_send(firstRow, LV_EVENT_CLICKED, nullptr);
  drain();
  check(timeline::snapshot().lastVisible == 239, "Double tap did not jump to latest");
  timeline::chatVirtJumpToOldest(&direct);
  drain();
  timeline::chatVirtJumpToLatest(&direct);
  drain();
  check(timeline::snapshot().lastVisible == 239, "Latest jump missed last message");
  // Same count with different ring endpoints must invalidate measured offsets.
  const auto height = timeline::snapshot().totalHeight;
  first = 240;
  longText = true;
  timeline::refreshChatDetail(direct);
  drain();
  check(timeline::snapshot().totalHeight > height, "Equal-count thread switch reused old row heights");
  auto *link = label(direct.msgs, "https://example.com/");
  check(link, "URL bubble missing");
  auto *row = lv_obj_get_parent(link);
  lv_event_send(row, LV_EVENT_LONG_PRESSED, nullptr);
  check(actions == 1, "Current message action was not dispatched");
  sequenceOffset = 500;
  lv_event_send(row, LV_EVENT_LONG_PRESSED, nullptr);
  lv_event_send(row, LV_EVENT_SHORT_CLICKED, nullptr);
  check(actions == 1 && !timeline::urlMenuOpen(), "Evicted message row acted on reused ring slot");
  sequenceOffset = 0;
  lv_event_send(row, LV_EVENT_SHORT_CLICKED, nullptr);
  check(timeline::urlMenuOpen(), "URL menu did not open");
  auto *oldMenu = lv_obj_get_child(lv_layer_top(), -1);
  auto *oldButton = lv_obj_get_parent(label(oldMenu, "QR"));
  timeline::closeUrlMenu();
  lv_event_send(lv_obj_get_parent(link), LV_EVENT_SHORT_CLICKED, nullptr);
  lv_event_send(oldButton, LV_EVENT_CLICKED, nullptr);
  check(timeline::urlMenuOpen() && !timeline::urlQrOpen(), "Old URL button acted on replacement menu");
  lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  check(!timeline::urlMenuOpen(), "External URL menu deletion retained root");
  drain();
  int before = reads;
  for (int i = 0; i < 8; ++i)
    timeline::refreshChatDetailAsync(direct);
  drain();
  check(reads == before + 1, "Detail refresh did not coalesce callbacks");
  timeline::refreshChatDetailAsync(direct);
  timeline::chatVirtScheduleRender(&direct);
  direct.detail_open = false;
  timeline::closed(&direct);
  before = reads;
  drain();
  check(reads == before && !timeline::snapshot().hasOffsets, "Closed chat retained deferred work");
  direct.detail_open = true;
  timeline::opened(0);
  timeline::refreshChatDetail(direct);
  timeline::refreshChatDetailAsync(direct);
  lv_obj_del(direct.msgs);
  drain();
  check(!direct.msgs && !timeline::snapshot().hasOffsets, "Deleted message container retained layout");
  createMessages(direct);
  timeline::refreshChatDetailAsync(direct);
  timeline::shutdown();
  before = reads;
  drain();
  check(reads == before, "Shutdown retained pending refresh");
  lv_obj_del(direct.msgs);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Timeline leaked LVGL roots");
  puts("Chat timeline: long history, scroll, thread switch, URL lifetime, coalescing and cancellation "
       "passed.");
}
