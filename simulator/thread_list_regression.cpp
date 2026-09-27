// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/MessageTypes.h"
#include "screens/ThreadListScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
struct Thread {
  char name[33];
  bool channel;
  uint16_t unread;
  uint32_t timestamp;
};
Thread threads[20];
int order[20];
int count, selected, menus;
bool compact, swipe;
uint32_t held;
char preview[80];
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool info(int index, bool &channel, uint16_t &unread, uint32_t &timestamp, char *name, size_t capacity) {
  if (index < 0 || index >= 20)
    return false;
  const auto &thread = threads[index];
  channel = thread.channel;
  unread = thread.unread;
  timestamp = thread.timestamp;
  snprintf(name, capacity, "%s", thread.name);
  return true;
}
ui::screens::ThreadListScreen::Host host(bool hardware = false) {
  return {[](bool, bool, int *indexes, int capacity) {
            for (int i = 0; i < count && i < capacity; ++i)
              indexes[i] = order[i];
            return count;
          },
          info,
          [](int index) { return index == 1; },
          [](int, char *sender, size_t senderCapacity, char *text, size_t textCapacity, bool *outgoing) {
            snprintf(sender, senderCapacity, "Alice");
            snprintf(text, textCapacity, "%s", preview);
            *outgoing = false;
            return true;
          },
          [] { return compact; },
          [](const char *, char *, size_t) { return false; },
          []() -> uint8_t { return 0; },
          [](const lv_font_t *, char *out, size_t capacity, const char *text) {
            snprintf(out, capacity, "%s", text);
          },
          [](int index, bool channel) {
            check(channel == threads[index].channel, "Thread selection changed kind");
            selected = index;
          },
          [](int index, const char *name, bool channel) {
            check(channel == threads[index].channel && !strcmp(name, threads[index].name),
                  "Thread action changed identity");
            ++menus;
          },
          hardware
          ? +[]() -> uint32_t { return held; }
          : nullptr,
          hardware ? +[] { return swipe; } : nullptr};
}
lv_obj_t *list(lv_obj_t *parent) {
  auto *object = lv_list_create(parent);
  lv_obj_set_size(object, 300, 140);
  return object;
}
bool textIn(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && strstr(lv_label_get_text(root), text))
    return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (textIn(lv_obj_get_child(root, i), text))
      return true;
  return false;
}
lv_obj_t *gear(lv_obj_t *row) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *child = lv_obj_get_child(row, i);
    if (lv_obj_has_flag(child, LV_OBJ_FLAG_USER_2))
      return child;
  }
  return nullptr;
}
void click(lv_obj_t *row) {
  lv_event_send(row, LV_EVENT_PRESSED, nullptr);
  lv_event_send(row, LV_EVENT_CLICKED, nullptr);
}
} // namespace
void runThreadListRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, 320, 200);
  auto *first = list(root), *second = list(root);
  count = 20;
  selected = -1;
  menus = 0;
  compact = true;
  held = 0;
  swipe = false;
  snprintf(preview, sizeof preview, "Original preview");
  for (int i = 0; i < count; ++i) {
    order[i] = i;
    snprintf(threads[i].name, sizeof threads[i].name, "Thread %d", i);
    threads[i].channel = i % 2;
    threads[i].unread = i == 1 ? 120 : 0;
    threads[i].timestamp = 1789948800;
  }
  {
    ui::screens::ThreadListScreen screen(host());
    screen.refresh(first, false, true);
    check(lv_obj_get_child_cnt(first) == 20 && textIn(first, "99+") && textIn(first, "@"),
          "Compact list lost rows/badges");
    auto *row = lv_obj_get_child(first, 0);
    click(row);
    check(selected == 0, "Thread row did not select conversation");
    auto *marker = lv_label_create(row);
    lv_label_set_text(marker, "unchanged marker");
    screen.refresh(first, false, true);
    check(textIn(first, "unchanged marker"), "Unchanged thread list rebuilt rows");
    auto *focusGroup = lv_group_create();
    auto *focusedGear = gear(row);
    lv_group_add_obj(focusGroup, row);
    lv_group_add_obj(focusGroup, focusedGear);
    lv_group_focus_obj(focusedGear);
    lv_obj_update_layout(first);
    lv_obj_scroll_to_y(first, 90, LV_ANIM_OFF);
    auto *unchangedRow = lv_obj_get_child(first, 0);
    auto *changingRow = lv_obj_get_child(first, 1);
    threads[1].unread = 7;
    screen.refresh(first, false, true);
    check(lv_obj_get_scroll_y(first) == 90, "Thread rebuild lost scroll offset");
    check(lv_obj_get_child(first, 0) == unchangedRow && textIn(first, "unchanged marker"),
          "One-row update recreated an unchanged row");
    check(lv_obj_get_child(first, 1) == changingRow && textIn(changingRow, "7"),
          "Changed unread row was not refreshed");
    check(lv_group_get_focused(focusGroup) == focusedGear && gear(unchangedRow) == focusedGear,
          "Unchanged focused gear lost identity during another row update");
    changingRow = lv_obj_get_child(first, 1);
    order[0] = 1;
    order[1] = 0;
    screen.refresh(first, false, true);
    check(lv_obj_get_child(first, 0) == changingRow && lv_obj_get_child(first, 1) == unchangedRow,
          "Reordering recreated existing rows");
    order[0] = 0;
    order[1] = 1;
    screen.refresh(first, false, true);
    check(lv_obj_get_child(first, 0) == unchangedRow && lv_obj_get_child(first, 1) == changingRow,
          "Restoring order recreated existing rows");
    check(lv_group_get_focused(focusGroup) == focusedGear,
          "Reordering moved focus off the same thread gear");
    row = lv_obj_get_child(first, 0);
    auto *settings = gear(row);
    check(settings != nullptr, "Thread gear missing");
    lv_event_send(settings, LV_EVENT_CLICKED, nullptr);
    check(menus == 1, "Thread gear did not open actions");
    selected = -1;
    lv_event_send(row, LV_EVENT_CLICKED, nullptr);
    check(selected == -1, "Gear gesture also opened conversation");
    click(row);
    check(selected == 0, "New gesture remained suppressed after gear");
    selected = -1;
    snprintf(threads[0].name, sizeof threads[0].name, "Reused slot");
    click(row);
    lv_event_send(settings, LV_EVENT_CLICKED, nullptr);
    check(selected == -1 && menus == 1, "Stale row callback used a reused thread slot");
    screen.refresh(first, false, true);
    check(lv_obj_get_child(first, 0) == unchangedRow && gear(unchangedRow) == focusedGear &&
              lv_group_get_focused(focusGroup) == focusedGear,
          "Changed name recreated the focused thread or gear");
    row = lv_obj_get_child(first, 0);
    lv_event_send(row, LV_EVENT_PRESSED, nullptr);
    lv_event_send(row, LV_EVENT_LONG_PRESSED, nullptr);
    lv_event_send(row, LV_EVENT_CLICKED, nullptr);
    check(menus == 2 && selected == -1, "Long press did not suppress matching click");
    compact = false;
    screen.refresh(first, false, true);
    check(textIn(first, "Original preview"), "Expanded list lost message preview");
    check(lv_obj_get_child(first, 0) == unchangedRow && gear(unchangedRow) == focusedGear &&
              lv_group_get_focused(focusGroup) == focusedGear,
          "Layout mode switch lost focused thread identity");
    auto *previewRow = lv_obj_get_child(first, 0);
    snprintf(preview, sizeof preview, "Changed without timestamp");
    screen.refresh(first, false, true);
    check(lv_obj_get_child(first, 0) == previewRow && textIn(first, "Changed without timestamp"),
          "Preview update recreated its row or was skipped");
    lv_group_del(focusGroup);
    auto *removedRow = lv_obj_get_child(first, 0);
    auto *survivor = lv_obj_get_child(first, 1);
    count = 19;
    for (int i = 0; i < count; ++i)
      order[i] = i + 1;
    screen.refresh(first, false, true);
    check(lv_obj_get_child_cnt(first) == 19 && lv_obj_get_child(first, 0) == survivor &&
              !lv_obj_is_valid(removedRow), "Removing one thread damaged surviving rows");
    count = 20;
    for (int i = 0; i < count; ++i)
      order[i] = i;
    screen.refresh(first, false, true);
    check(lv_obj_get_child(first, 1) == survivor, "Adding a thread recreated a surviving row");
    auto *oldRow = lv_obj_get_child(first, 1);
    screen.refresh(second, true, false);
    selected = -1;
    click(oldRow);
    check(selected == -1, "Rebound list accepted old row callback");
    lv_obj_del(first);
    auto *liveRow = lv_obj_get_child(second, 1);
    click(liveRow);
    check(selected == 1, "Old tree deletion damaged replacement callbacks");
    lv_obj_del(liveRow);
    screen.refresh(second, true, false);
    check(lv_obj_get_child_cnt(second) == 20, "Externally deleted row did not invalidate list");
  }
  selected = -1;
  click(lv_obj_get_child(second, 0));
  check(selected == -1, "Destroyed list owner accepted callback from borrowed tree");
  lv_obj_del(second);
  auto *hardware = list(root);
  {
    ui::screens::ThreadListScreen screen(host(true));
    count = 2;
    screen.refresh(hardware, false, true);
    auto *row = lv_obj_get_child(hardware, 0);
    const int before = menus;
    lv_event_send(row, LV_EVENT_PRESSED, nullptr);
    held = 790;
    lv_event_send(row, LV_EVENT_PRESSING, nullptr);
    check(menus == before, "Hardware long press fired before threshold");
    held = 900;
    swipe = true;
    lv_event_send(row, LV_EVENT_PRESSING, nullptr);
    check(menus == before, "Swipe opened hardware thread actions");
    swipe = false;
    lv_event_send(row, LV_EVENT_PRESSING, nullptr);
    lv_event_send(row, LV_EVENT_PRESSING, nullptr);
    check(menus == before + 1, "Hardware hold did not fire exactly once");
    lv_obj_del(hardware);
  }
  lv_obj_del(root);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Thread list regression leaked roots");
}
