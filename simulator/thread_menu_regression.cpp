// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/ThreadActionMenu.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
namespace menu = ui::screens::threadMenu;
menu::Thread records[2];
int marks, erases, clears, icons, copies, regions, logins, joins, resets, blocks, lastIndex, cancels;
uint8_t muteFlags;
menu::IconResult picker;
uint32_t pickerRequest;
lv_obj_t *activeShare;
char copied[33];
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void closeRoot(lv_obj_t **root) {
  if (*root)
    lv_obj_add_flag(*root, LV_OBJ_FLAG_HIDDEN);
  *root = nullptr;
}
lv_obj_t *lastRoot() { return lv_obj_get_child(lv_layer_top(), lv_obj_get_child_cnt(lv_layer_top()) - 1); }
lv_obj_t *button(lv_obj_t *root, const char *text) {
  bool matches = false;
  if (lv_obj_check_type(root, &lv_label_class)) {
    // DOT temporarily replaces the label bytes. These tests address actions,
    // so inspect the restored source text, then retain the production long mode.
    const auto mode = lv_label_get_long_mode(root);
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(root, LV_LABEL_LONG_CLIP);
    matches = strstr(lv_label_get_text(root), text) != nullptr;
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(root, mode);
  }
  if (matches) {
    auto *parent = lv_obj_get_parent(root);
    while (parent && !lv_obj_check_type(parent, &lv_btn_class))
      parent = lv_obj_get_parent(parent);
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = button(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *expectButton(const char *text) {
  lv_obj_update_layout(lastRoot());
  auto *found = button(lastRoot(), text);
  if (!found) {
    auto dump = [](auto &&self, lv_obj_t *object) -> void {
      if (lv_obj_check_type(object, &lv_label_class))
        fprintf(stderr, "Menu label: %s\n", lv_label_get_text(object));
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
        self(self, lv_obj_get_child(object, i));
    };
    dump(dump, lastRoot());
    throw std::runtime_error(std::string("Thread menu button missing: ") + text);
  }
  return found;
}
void click(lv_obj_t *object) {
  check(object != nullptr, "Clicked missing thread action");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}
void act(const char *text) { click(expectButton(text)); }
menu::Host host() {
  return {[](int index, menu::Thread &out) {
            if (index < 1 || index > 2)
              return false;
            out = records[index - 1];
            return out.index == index;
          },
          [] { return 24; },
          closeRoot,
          [] {},
          [](lv_obj_t *) {},
          [](const lv_font_t *, char *out, size_t capacity, const char *text) {
            snprintf(out, capacity, "%s", text);
          },
          [](const char *, unsigned) {},
          [](int index) {
            ++marks;
            lastIndex = index;
          },
          [](const menu::Thread &thread) {
            ++erases;
            lastIndex = thread.index;
          },
          [](int index) {
            ++clears;
            lastIndex = index;
          },
          [](const uint8_t *) { ++resets; },
          [](const uint8_t *) { ++logins; },
          [](const uint8_t *) { ++joins; },
          [](int slot, const char *) {
            ++regions;
            lastIndex = slot;
          },
          [] { ++blocks; },
          [](const char *) { return muteFlags; },
          [](const char *, uint8_t flags) { muteFlags = flags; },
          [](const char *name, const char *) {
            check(!strcmp(name, records[0].name), "Icon applied to another thread");
            ++icons;
          },
          [](menu::IconResult result, uint32_t request) {
            picker = result;
            pickerRequest = request;
          },
          [](menu::IconResult result) {
            if (picker == result) {
              picker = nullptr;
              ++cancels;
            }
          },
          [] {
            auto *root = lv_obj_create(lv_layer_top());
            activeShare = lv_obj_create(root);
            return activeShare;
          },
          [](lv_obj_t *body) { return body && body == activeShare; },
          [](lv_obj_t *body) {
            if (body == activeShare) {
              lv_obj_add_flag(lv_obj_get_parent(body), LV_OBJ_FLAG_HIDDEN);
              activeShare = nullptr;
            }
          },
          [](const char *secret) {
            ++copies;
            snprintf(copied, sizeof copied, "%s", secret);
          }};
}
} // namespace
void runThreadMenuPickerRegression(int index, const char *name, void (*pump)(unsigned)) {
  const uint8_t language = i18nGetLang();
  i18nSetLang(LANG_EN);
  char previous[20] = {};
  touchPrefsGetChannelEmoji(name, previous, sizeof previous);
  auto firstButton = [](auto &&self, lv_obj_t *object) -> lv_obj_t * {
    if (lv_obj_check_type(object, &lv_btn_class))
      return object;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
      if (auto *found = self(self, lv_obj_get_child(object, i)))
        return found;
    return nullptr;
  };
  menu::show(index);
  act("Chat icon");
  auto *oldPicker = lastRoot();
  auto *oldGlyph = firstButton(firstButton, oldPicker);
  check(oldGlyph, "Production icon picker has no glyph button");
  menu::show(index);
  act("Chat icon");
  auto *newPicker = lastRoot();
  auto *newGlyph = firstButton(firstButton, newPicker);
  check(newGlyph, "Replacement icon picker has no glyph button");
  auto *label = lv_obj_get_child(newGlyph, 0);
  check(lv_obj_check_type(label, &lv_label_class), "Simulator icon fixture changed");
  char expected[20];
  snprintf(expected, sizeof expected, "%s", lv_label_get_text(label));
  click(oldGlyph);
  click(oldPicker);
  char value[20] = {};
  touchPrefsGetChannelEmoji(name, value, sizeof value);
  check(!strcmp(previous, value), "Old production picker changed channel icon");
  click(newGlyph);
  touchPrefsGetChannelEmoji(name, value, sizeof value);
  check(!strcmp(expected, value), "Old picker event cancelled replacement selection");
  menu::show(index);
  act("Chat icon");
  lv_obj_del(lastRoot());
  menu::close();
  menu::show(index);
  act("Chat icon");
  check(firstButton(firstButton, lastRoot()), "Picker could not reopen after external DELETE");
  menu::close();
  touchPrefsSetChannelEmoji(name, previous);
  i18nSetLang(language);
  pump(30);
}
void runThreadMenuRegression() {
  const auto rootCount = lv_obj_get_child_cnt(lv_layer_top());
  const uint8_t language = i18nGetLang();
  i18nSetLang(LANG_EN);
  records[0] = menu::Thread{};
  records[0].index = 1;
  records[0].channel = true;
  records[0].channelSlot = 7;
  records[0].hasSecret = true;
  memset(records[0].secret, 0x12, 16);
  snprintf(records[0].name, sizeof records[0].name, "Channel");
  records[1] = menu::Thread{};
  records[1].index = 2;
  records[1].hasContact = true;
  records[1].room = true;
  memset(records[1].contact, 0x42, 32);
  snprintf(records[1].name, sizeof records[1].name, "Room");
  marks = erases = clears = icons = copies = regions = logins = joins = resets = blocks = cancels = 0;
  muteFlags = 0;
  picker = nullptr;
  activeShare = nullptr;
  menu::configure(host());
  menu::show(1);
  auto *oldMark = expectButton("Mark as read");
  menu::show(2);
  click(oldMark);
  check(marks == 0, "Old menu acted on replacement thread");
  act("Mark as read");
  check(marks == 1 && lastIndex == 2 && !menu::isOpen(), "Mark read contract failed");
  click(oldMark);
  check(marks == 1, "Closed menu accepted old callback");
  menu::show(1);
  act("Remove channel");
  auto *oldConfirm = expectButton("Remove");
  records[0].secret[0] ^= 1;
  click(oldConfirm);
  check(erases == 0, "Changed channel secret passed delayed deletion");
  menu::show(2);
  act("Delete chat");
  records[1].contact[0] ^= 1;
  act("Delete");
  check(erases == 0, "Replaced contact identity passed delayed deletion");
  menu::show(1);
  act("Delete history");
  auto *oldHistory = expectButton("Delete");
  menu::show(2);
  click(oldHistory);
  check(clears == 0, "Old confirmation cleared replacement history");
  act("Delete history");
  act("Delete");
  check(clears == 1 && lastIndex == 2, "Current history confirmation failed");
  menu::show(1);
  act("Remove channel");
  act("Remove");
  check(erases == 1 && lastIndex == 1, "Current channel confirmation failed");
  menu::show(1);
  act("Mute msgs");
  check(muteFlags == 1, "Message mute toggle failed");
  act("Mute @");
  check(muteFlags == 3, "Mention mute toggle failed");
  act("Unmute msgs");
  check(muteFlags == 2, "Unmute label/toggle failed");
  auto *muteButton = expectButton("Mute msgs");
  lv_obj_del(muteButton);
  act("Unmute @");
  check(muteFlags == 0, "Deleted mute control retained pointer");
  act("Chat icon");
  auto previousPicker = picker;
  auto previousRequest = pickerRequest;
  menu::show(2);
  previousPicker(previousRequest, "X");
  check(icons == 0 && cancels > 0, "Closed icon request affected another menu");
  menu::show(1);
  act("Chat icon");
  previousPicker(previousRequest, "X");
  check(icons == 0, "Old icon request accepted by new picker");
  picker(pickerRequest, "Y");
  check(icons == 1, "Current icon request failed");
  act("Chat icon");
  auto deletedPicker = picker;
  auto deletedRequest = pickerRequest;
  lv_obj_del(lastRoot());
  deletedPicker(deletedRequest, "Z");
  check(icons == 1 && !menu::isOpen(), "Deleted menu retained icon action");
  menu::show(1);
  act("Share secret");
  auto *oldCopy = expectButton("Copy secret");
  click(oldCopy);
  check(copies == 1 && strlen(copied) == 32, "Share secret copy failed");
  menu::show(1);
  act("Share secret");
  click(oldCopy);
  check(copies == 1, "Old share dialog copied replacement secret");
  act("Copy secret");
  check(copies == 2, "Current share copy failed");
  activeShare = nullptr;
  act("Copy secret");
  check(copies == 2, "Closed settings share remained actionable");
  menu::show(1);
  records[0].channelSlot = 9;
  act("Region & scope");
  check(regions == 1 && lastIndex == 9, "Region action used stale channel slot");
  menu::show(2);
  act("Log in again");
  check(logins == 1, "Room login command missing");
  menu::show(2);
  act("Join w/ password");
  check(joins == 1, "Room join command missing");
  menu::show(2);
  act("Reset path");
  check(resets == 1, "Reset path command missing");
  menu::show(2);
  act("Blocked users");
  check(blocks == 1, "Blocked users command missing");
  menu::show(2);
  auto *staleReset = expectButton("Reset path");
  snprintf(records[1].name, sizeof records[1].name, "Reused slot");
  click(staleReset);
  check(resets == 1, "Reused thread slot accepted old action");
  menu::close();
  while (lv_obj_get_child_cnt(lv_layer_top()) > rootCount)
    lv_obj_del(lastRoot());
  menu::configure({});
  i18nSetLang(language);
  check(!menu::isOpen() && lv_obj_get_child_cnt(lv_layer_top()) == rootCount,
        "Thread menu regression leaked roots");
}
