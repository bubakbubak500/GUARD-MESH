// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/MessageActionMenu.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
namespace menu = ui::screens::messageMenu;
menu::Message message{};
bool active = true;
int copies = 0, erased = 0, sent = 0, info = 0, blocked = 0, inserts = 0;
char inserted[200]{};
void check(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}
lv_obj_t *label(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text))
    return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = label(lv_obj_get_child(object, i), text))
      return found;
  return nullptr;
}
lv_obj_t *button(const char *text) {
  auto *found = label(lv_obj_get_child(lv_layer_top(), -1), TR(text));
  check(found, "Message menu button missing");
  return lv_obj_get_parent(found);
}
void click(const char *text) { lv_event_send(button(text), LV_EVENT_CLICKED, nullptr); }
} // namespace
void runMessageMenuRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  message = {};
  message.seq = 12;
  message.channel = true;
  strcpy(message.sender, "peer");
  strcpy(message.text, "captured message");
  message.meta_flags = ui::MessageTypes::MSG_META_HAS_RX;
  message.snr_q4 = 24;
  message.rssi = -90;
  menu::Host host{};
  host.readMessage = [](int index, menu::Message &out) {
    out = message;
    return index == 7;
  };
  host.activeConversation = [](const menu::Message &) { return active; };
  host.statusHeight = [] { return 24; };
  host.closeRoot = [](lv_obj_t **root) {
    lv_obj_del_async(*root);
    *root = nullptr;
  };
  host.closeInfo = [] {};
  host.copy = [](const char *text) {
    check(!strcmp(text, "captured message"), "Copy lost message snapshot");
    ++copies;
  };
  host.info = [](int index) {
    check(index == 7, "Info selected wrong record");
    ++info;
  };
  host.insert = [](bool channel, const char *text) {
    check(channel, "Wrong composer");
    snprintf(inserted, sizeof inserted, "%s", text);
    ++inserts;
  };
  host.block = [](const char *sender) {
    check(!strcmp(sender, "peer"), "Block selected wrong sender");
    ++blocked;
  };
  host.resend = [](const char *text) {
    check(!strcmp(text, "captured message"), "Resend lost snapshot");
    ++sent;
  };
  host.erase = [](int index) {
    check(index == 7, "Delete selected wrong record");
    ++erased;
  };
  menu::configure(host);
  menu::show(7);
  auto *oldDelete = button(LV_SYMBOL_TRASH "  Delete");
  menu::show(7);
  lv_event_send(oldDelete, LV_EVENT_CLICKED, nullptr);
  check(menu::isOpen() && !erased, "Old message menu deleted replacement selection");
  click(LV_SYMBOL_COPY "  Copy");
  check(copies == 1 && !menu::isOpen(), "Copy failed to close menu");
  pump(40);
  menu::show(7);
  ++message.seq;
  click(LV_SYMBOL_TRASH "  Delete");
  check(!erased, "Delete acted on reused ring slot");
  pump(40);
  menu::show(7);
  active = false;
  click(LV_SYMBOL_CLOSE "  Block");
  check(!blocked, "Old menu blocked sender in another conversation");
  active = true;
  pump(40);
  menu::show(7);
  click(LV_SYMBOL_OK "  Ack");
  check(inserts == 1 && strstr(inserted, "@[peer], ack: SNR 6.0 dB, RSSI -90 dBm"),
        "ACK lost captured metadata");
  pump(40);
  menu::show(7);
  click(LV_SYMBOL_LIST "  Info");
  check(info == 1, "Info did not dispatch");
  pump(40);
  menu::show(7);
  click(LV_SYMBOL_TRASH "  Delete");
  check(erased == 1, "Valid delete did not dispatch");
  pump(40);
  message.outgoing = true;
  menu::show(7);
  click(LV_SYMBOL_REFRESH "  Resend");
  check(sent == 1, "Valid resend did not dispatch");
  pump(40);
  menu::show(7);
  lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  check(!menu::isOpen(), "Deleted menu retained root");
  menu::show(7);
  auto *detached = button(LV_SYMBOL_REFRESH "  Resend");
  menu::configure(host);
  lv_event_send(detached, LV_EVENT_CLICKED, nullptr);
  check(sent == 1, "Reconfigured menu retained action");
  pump(40);
  message.deliv_state = ui::MessageTypes::DELIV_FAILED;
  menu::retry(7);
  active = false;
  click("Resend");
  check(sent == 1 && !menu::isOpen(), "Retry confirmation sent to another conversation");
  active = true;
  pump(40);
  menu::retry(7);
  auto *oldRetry = button("Resend");
  menu::close();
  lv_event_send(oldRetry, LV_EVENT_CLICKED, nullptr);
  check(sent == 1, "Closed retry retained confirmation");
  pump(40);
  menu::retry(7);
  ++message.seq;
  click("Resend");
  check(sent == 1, "Retry used an evicted record");
  pump(40);
  menu::retry(7);
  click("Resend");
  check(sent == 2 && !menu::isOpen(), "Valid retry did not send once");
  pump(40);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Message menu leaked roots");
}
