// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/GpsSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Screen = ui::screens::GpsSettingsScreen;
using Settings = ui::GpsSettings;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
lv_obj_t *find(lv_obj_t *root, const lv_obj_class_t *type) {
  if (lv_obj_check_type(root, type))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *result = find(lv_obj_get_child(root, i), type))
      return result;
  return nullptr;
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *result = label(lv_obj_get_child(root, i), text))
      return result;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *caption = label(root, text);
  check(caption, "GPS privacy button missing");
  return lv_obj_get_parent(caption);
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
struct Context {
  Screen *screen = nullptr;
  lv_obj_t *current = nullptr, *replacement = nullptr;
  ui::gps::Snapshot position;
  unsigned reads = 0, changes = 0, alerts = 0;
  bool eraseOnRead = false, replaceOnRead = false, replaceOnSet = false;
};
} // namespace
void runGpsSettingsRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Context context;
  Settings settings({&context,
                     [](void *p, ui::gps::Snapshot &position) {
                       auto &c = *static_cast<Context *>(p);
                       ++c.reads;
                       position = c.position;
                       if (c.eraseOnRead) {
                         c.eraseOnRead = false;
                         lv_obj_del(c.current);
                         c.current = nullptr;
                       }
                       if (c.replaceOnRead) {
                         c.replaceOnRead = false;
                         c.screen->build(c.replacement, 202, true);
                       }
                     },
                     [](void *p, bool on) {
                       auto &c = *static_cast<Context *>(p);
                       if (c.position.enabled != on) {
                         c.position.enabled = on;
                         ++c.changes;
                       }
                       if (c.replaceOnSet) {
                         c.replaceOnSet = false;
                         c.screen->build(c.replacement, 202, false);
                       }
                     },
                     38400});
  // Preserve an unset baud as well as an explicit baud preference.
  const auto originalBaud = touchPrefsGetGpsBaud(0);
  const auto originalPrivacy = touchPrefsGetGpsFuzzM();
  settings.setBaud(2);
  settings.setPrivacy(0);
  check(!settings.setBaud(5) && !settings.setPrivacy(4), "GPS service accepted an invalid choice");
  Screen::Host host{&context, [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
                    nullptr};
  Screen screen(settings, host);
  context.screen = &screen;
  auto *first = context.current = body();
  screen.build(first, 202, true);
  auto *toggle = find(first, &lv_switch_class);
  auto *baud = find(first, &lv_dropdown_class);
  check(toggle && baud && lv_dropdown_get_selected(baud) == 2 && label(first, TR("GPS: off")),
        "GPS page did not load the backend snapshot");
  const auto children = lv_obj_get_child_cnt(first);
  lv_obj_add_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(context.changes == 1, "GPS enable did not reach backend");
  settings.acquisition(true, 0);
  screen.refresh(61000);
  lv_obj_update_layout(first);
  for (uint32_t i = 1; i < lv_obj_get_child_cnt(first); ++i) {
    auto *previous = lv_obj_get_child(first, i - 1), *current = lv_obj_get_child(first, i);
    check(lv_obj_get_y(current) >= lv_obj_get_y(previous) + lv_obj_get_height(previous),
          "Growing GPS status overlaps its settings");
  }
  context.position.fix = true;
  context.position.latitude = 50;
  context.position.longitude = 14;
  screen.refresh(62000);
  auto *privacy = button(first, "250 m");
  click(privacy);
  check(touchPrefsGetGpsFuzzM() == 250 && lv_obj_get_child_cnt(first) == children,
        "GPS privacy rebuilt the page or failed to persist");
  lv_dropdown_set_selected(baud, 4);
  lv_event_send(baud, LV_EVENT_VALUE_CHANGED, nullptr);
  check(touchPrefsGetGpsBaud(0) == 115200, "GPS baud not persisted");
  lv_dropdown_open(baud);
  auto *second = context.current = body();
  screen.build(second, 202, false);
  check(!lv_dropdown_is_open(baud), "Retired GPS form left its dropdown open");
  click(privacy);
  lv_obj_clear_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_dropdown_set_selected(baud, 0);
  lv_event_send(baud, LV_EVENT_VALUE_CHANGED, nullptr);
  check(context.changes == 1 && touchPrefsGetGpsBaud(0) == 115200 && touchPrefsGetGpsFuzzM() == 250,
        "Retired GPS controls changed live state");
  check(!find(second, &lv_switch_class) && !find(second, &lv_dropdown_class) &&
            label(second, TR("No GPS module on this device.")),
        "No-GPS page exposes hardware controls");
  click(button(second, "1 km"));
  check(touchPrefsGetGpsFuzzM() == 1000, "No-GPS board lost location privacy");
  lv_obj_del(first);
  const auto count = lv_obj_get_child_cnt(second);
  screen.build(second, 202, false);
  check(lv_obj_get_child_cnt(second) == count, "Rebuilt GPS form accumulated controls");
  context.eraseOnRead = true;
  int deletes = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        second, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deletes);
  screen.refresh(63000);
  check(deletes == 3, "GPS teardown skipped later DELETE observers");
  const auto reads = context.reads;
  screen.refresh(64000);
  check(context.reads == reads, "Deleted GPS form keeps polling");

  auto *third = context.current = body();
  screen.build(third, 202, true);
  auto *replacement = context.replacement = body();
  context.replaceOnRead = true;
  screen.refresh(65000);
  check(find(replacement, &lv_switch_class), "Reentrant GPS refresh lost replacement");
  lv_obj_del(third);
  context.current = replacement;
  auto *last = context.replacement = body();
  context.replaceOnSet = true;
  toggle = find(replacement, &lv_switch_class);
  lv_obj_clear_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(label(last, TR("No GPS module on this device.")) && !find(last, &lv_switch_class),
        "GPS enable callback overwrote a replacement form");
  lv_obj_del(replacement);
  lv_obj_del(last);
  auto *retained = body();
  {
    Screen temporary(settings, host);
    temporary.build(retained, 202, false);
  }
  click(button(retained, TR("Exact")));
  check(touchPrefsGetGpsFuzzM() == 1000, "GPS control retained a destroyed owner");
  lv_obj_del(retained);
  screen.detach();
  touchPrefsSetGpsBaud(originalBaud);
  touchPrefsSetGpsFuzzM(originalPrivacy);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "GPS screen leaked LVGL roots");
  puts("GPS settings: snapshots, real preferences, growing layout, capability, stale events and reentrancy "
       "passed.");
}
