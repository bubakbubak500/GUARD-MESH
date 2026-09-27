// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/MessageInfoScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
namespace info = ui::screens::messageInfo;
info::Message message{};
bool active = true;
int traces = 0, replays = 0, copies = 0, prepared = 0;
uint32_t routeSequence = 0;
void check(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && strstr(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *button(const char *text) {
  auto *found = label(lv_obj_get_child(lv_layer_top(), -1), TR(text));
  check(found, "Message info button missing");
  return lv_obj_get_parent(found);
}
} // namespace
void runMessageInfoRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  message = {};
  message.seq = 101;
  message.ts = 1750000000;
  strcpy(message.sender, "peer");
  message.meta_flags = ui::MessageTypes::MSG_META_HAS_RX | ui::MessageTypes::MSG_META_IS_FLOOD;
  message.path_len = 32;
  message.in_path_n = 32;
  for (int i = 0; i < 32; ++i)
    message.in_path[i] = i;
  info::Host host{};
  host.readMessage = [](int index, info::Message &out) {
    out = message;
    return index == 3;
  };
  host.activeConversation = [](const info::Message &) { return active; };
  host.statusHeight = [] { return 24; };
  host.closeRoot = [](lv_obj_t **root) {
    lv_obj_del_async(*root);
    *root = nullptr;
  };
  host.prepareRoute = [](const info::Message &source) {
    ++prepared;
    routeSequence = source.seq;
    return 3;
  };
  host.trace = [] { ++traces; };
  host.replay = [] { ++replays; };
  host.regionName = [](uint8_t) { return "test-region"; };
  host.repeats = [](uint32_t) -> uint8_t { return 32; };
  host.repeatCount = [](uint32_t) -> uint8_t { return 32; };
  host.repeatHop = [](uint32_t, uint8_t index, uint8_t *hash, uint8_t) -> uint8_t {
    hash[0] = index;
    return 1;
  };
  host.hopName = [](const uint8_t *, int, char *out, size_t capacity) {
    snprintf(out, capacity, "long-repeater-name-0123456789012");
    return true;
  };
  host.sanitize = [](const lv_font_t *, char *out, size_t capacity, const char *text) {
    snprintf(out, capacity, "%s", text);
  };
  host.copyLabel = [](lv_event_t *event) {
    check(!strcmp(static_cast<const char *>(lv_event_get_user_data(event)), "info"),
          "Copy lost label context");
    ++copies;
  };
  host.clampScroll = [](lv_event_t *) {};
  info::configure(host);
  info::show(3);
  check(info::isOpen() && info::scrollBody(), "Message info did not create body");
  auto *dump = lv_obj_get_child(info::scrollBody(), 0);
  check(strlen(lv_label_get_text(dump)) < 1400 && strstr(lv_label_get_text(dump), "Route"),
        "Long route overflowed or disappeared");
  lv_event_send(dump, LV_EVENT_LONG_PRESSED, nullptr);
  check(copies == 1, "Metadata copy did not dispatch");
  auto *oldTrace = button("Trace");
  info::show(3);
  lv_event_send(oldTrace, LV_EVENT_CLICKED, nullptr);
  check(!traces && info::isOpen(), "Old info button acted on new popup");
  pump(40);
  active = false;
  lv_event_send(button("Trace"), LV_EVENT_CLICKED, nullptr);
  check(!traces, "Trace targeted a different conversation");
  active = true;
  lv_event_send(button("Trace"), LV_EVENT_CLICKED, nullptr);
  check(traces == 1 && !info::isOpen(), "Valid trace did not close info");
  pump(40);
  info::show(3);
  const auto captured = message.seq;
  ++message.seq;
  lv_event_send(button("Replay"), LV_EVENT_CLICKED, nullptr);
  check(replays == 1 && routeSequence == captured && prepared >= 3,
        "Replay used replaced global route instead of snapshot");
  pump(40);
  message.outgoing = true;
  message.sent_fp = 123;
  info::show(3);
  dump = lv_obj_get_child(info::scrollBody(), 0);
  check(strlen(lv_label_get_text(dump)) < 1400, "Repeated hops overflowed metadata");
  lv_obj_del(info::scrollBody());
  check(!info::scrollBody() && info::isOpen(), "Deleted body retained borrowed pointer");
  lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  check(!info::isOpen(), "Deleted info retained root");
  info::showTraceResult("trace one", "body one");
  auto *oldRoot = lv_obj_get_child(lv_layer_top(), -1);
  info::showTraceResult("trace two", "body two");
  lv_event_send(oldRoot, LV_EVENT_CLICKED, nullptr);
  check(info::traceResultOpen(), "Old trace result closed replacement");
  pump(40);
  lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  check(!info::traceResultOpen(), "Deleted trace result retained root");
  info::show(3);
  oldTrace = button("Trace");
  info::configure(host);
  lv_event_send(oldTrace, LV_EVENT_CLICKED, nullptr);
  check(traces == 1, "Reconfigured info retained trace callback");
  pump(40);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Message info leaked roots");
}
