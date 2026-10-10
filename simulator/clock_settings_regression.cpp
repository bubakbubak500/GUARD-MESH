// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "platform/UiPlatform.h"
#include "screens/ClockSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Screen = ui::screens::ClockSettingsScreen;
using Settings = ui::ClockSettings;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *field(lv_obj_t *root) {
  if (lv_obj_check_type(root, &lv_textarea_class))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = field(lv_obj_get_child(root, i)))
      return found;
  return nullptr;
}
lv_obj_t *findButton(lv_obj_t *root, const char *text) {
  // The offset value itself can read "+1 h" too; match a button, not
  // the first label with that text.
  if (lv_obj_check_type(root, &lv_btn_class) && label(root, text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *child = lv_obj_get_child(root, i);
    if (auto *found = findButton(child, text))
      return found;
  }
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *found = findButton(root, text);
  if (!found)
    throw std::runtime_error(std::string("Clock settings button missing: ") + text);
  return found;
}
lv_obj_t *toggle(lv_obj_t *root, const char *text) {
  auto *caption = label(root, text);
  check(caption, "Clock settings switch label missing");
  auto *row = lv_obj_get_parent(caption);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *object = lv_obj_get_child(row, i);
    if (lv_obj_check_type(object, &lv_switch_class))
      return object;
  }
  throw std::runtime_error("Clock settings switch missing");
}
lv_obj_t *zoneButton(lv_obj_t *root) {
  // LONG_DOT may temporarily replace the caption's tail after layout.
  // The form has three direct buttons: sync, manual set, timezone.
  unsigned buttons = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *child = lv_obj_get_child(root, i);
    if (lv_obj_check_type(child, &lv_btn_class) && ++buttons == 3)
      return child;
  }
  throw std::runtime_error("Timezone form control missing");
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}
struct Context {
  Screen *screen = nullptr;
  lv_obj_t *current = nullptr, *replacement = nullptr;
  uint32_t epoch = 1704067200;
  int writes = 0, syncs = 0, alerts = 0;
  bool syncOk = false, replaceOnSync = false, replaceOnClose = false;
  const char *mirror = nullptr;
};
Context *closingContext = nullptr;
void closeLater(lv_obj_t **object) {
  auto *old = *object;
  *object = nullptr;
  lv_obj_del_async(old);
  if (closingContext && closingContext->replaceOnClose) {
    closingContext->replaceOnClose = false;
    closingContext->screen->build(closingContext->replacement, 202, true);
  }
}
} // namespace
void runClockSettingsRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Context context;
  closingContext = &context;
  Settings settings({&context, [](void *p) { return static_cast<Context *>(p)->epoch; },
                     [](void *p, uint32_t epoch) {
                       auto &c = *static_cast<Context *>(p);
                       c.epoch = epoch;
                       ++c.writes;
                     },
                     [](void *p) {
                       auto &c = *static_cast<Context *>(p);
                       ++c.syncs;
                       return c.syncOk;
                     },
                     1600000000});
  const auto original = settings.read();
  settings.setZone(2);
  touchPrefsSetTimeOffsetHours(0);
  settings.setFlag(Settings::Flag::Hour12, false);
  settings.setFlag(Settings::Flag::BootWifi, false);
  settings.setFlag(Settings::Flag::OpenWifi, false);
  Screen::Host host{&context,
                    nullptr,
                    [](void *p) {
                      auto &c = *static_cast<Context *>(p);
                      if (c.replaceOnSync) {
                        c.replaceOnSync = false;
                        c.screen->build(c.replacement, 202, true);
                      } else if (c.mirror)
                        lv_textarea_set_text(field(c.current), c.mirror);
                    },
                    [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
                    closeLater,
                    [] { return lv_coord_t(24); }};
  Screen screen(settings, host);
  context.screen = &screen;
  auto *first = context.current = body();
  screen.build(first, 202, true);
  check(!strcmp(lv_textarea_get_text(field(first)), "2024-01-01 00:00"), "Clock prefill lost local time");
  click(button(first, TR("Sync clock from system")));
  check(context.syncs == 1, "Clock sync did not reach backend");
  auto *manual = button(first, TR("Set clock"));
  lv_textarea_set_text(field(first), "2024-02-31 12:00");
  click(manual);
  check(context.writes == 0, "Invalid calendar day was normalized and saved");
  lv_textarea_set_text(field(first), "2000-01-01 12:00");
  click(manual);
  check(context.writes == 0, "Manual clock bypassed replay floor");
  lv_textarea_set_text(field(first), "2024-2-01 12:00");
  click(manual);
  check(context.writes == 0, "Malformed manual date was saved");
  context.mirror = "2024-02-29 12:34";
  click(manual);
  context.mirror = nullptr;
  check(context.writes == 1, "Manual clock did not synchronize keyboard draft");
  char local[20];
  settings.prefill(local, sizeof local);
  check(!strcmp(local, "2024-02-29 12:34"), "Manual local-time conversion changed the entered stamp");
  auto *boot = toggle(first, TR("Sync time from saved Wi-Fi after cold boot"));
  auto *open = toggle(first, TR("Allow saved open networks for time sync"));
  check(lv_obj_has_state(open, LV_STATE_DISABLED), "Open-network switch ignored disabled boot sync");
  lv_obj_add_state(open, LV_STATE_CHECKED);
  lv_event_send(open, LV_EVENT_VALUE_CHANGED, nullptr);
  check(!settings.read().openWifi, "Disabled open-network switch changed preferences");
  lv_obj_add_state(boot, LV_STATE_CHECKED);
  lv_event_send(boot, LV_EVENT_VALUE_CHANGED, nullptr);
  check(settings.read().bootWifi && !lv_obj_has_state(open, LV_STATE_DISABLED),
        "Boot sync did not enable dependent switch");
  lv_event_send(open, LV_EVENT_VALUE_CHANGED, nullptr);
  check(settings.read().openWifi, "Open-network preference not saved");
  auto *hour = toggle(first, TR("12-hour clock"));
  lv_obj_add_state(hour, LV_STATE_CHECKED);
  lv_event_send(hour, LV_EVENT_VALUE_CHANGED, nullptr);
  check(settings.read().hour12, "Clock format preference not saved");
  for (int i = 0; i < 30; ++i)
    click(button(first, "+1 h"));
  check(settings.read().offset == 23 && label(first, "+23 h"), "Positive UTC offset exceeded its bound");
  for (int i = 0; i < 60; ++i)
    click(button(first, "-1 h"));
  check(settings.read().offset == -23 && label(first, "-23 h"), "Negative UTC offset exceeded its bound");
  lv_obj_update_layout(first);
  for (uint32_t i = 1; i < lv_obj_get_child_cnt(first); ++i) {
    auto *previous = lv_obj_get_child(first, i - 1), *current = lv_obj_get_child(first, i);
    check(lv_obj_get_y(current) >= lv_obj_get_y(previous) + lv_obj_get_height(previous),
          "Clock flex rows overlap");
  }
  click(zoneButton(first));
  check(screen.pickerOpen(), "Clock timezone picker did not open");
  auto *oldChoice = button(lv_layer_top(), settings.zoneLabel(1));
  auto *second = context.current = body();
  screen.build(second, 202, true);
  click(oldChoice);
  click(manual);
  check(settings.read().zone == 2 && context.writes == 1,
        "Old clock form or picker changed replacement state");
  pump(40);
  lv_obj_del(first);
  // Programmatic clicks do not guarantee a display refresh. Commit the new
  // form's percentage widths before inspecting its LONG_DOT caption.
  lv_obj_update_layout(second);
  click(zoneButton(second));
  click(button(lv_layer_top(), settings.zoneLabel(1)));
  check(settings.read().zone == 1 && !screen.pickerOpen() && label(second, settings.zoneLabel(1)),
        "Timezone selection did not update the form");
  pump(40);
  const auto childCount = lv_obj_get_child_cnt(second);
  screen.build(second, 202, true);
  check(lv_obj_get_child_cnt(second) == childCount, "Rebuilding clock form accumulated old controls");
  click(zoneButton(second));
  int deletes = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        second, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deletes);
  lv_obj_del(second);
  check(deletes == 3, "Clock teardown skipped later DELETE observers");
  pump(40);
  check(!screen.pickerOpen(), "Deleting clock form leaked its timezone picker");

  auto *third = context.current = body();
  screen.build(third, 202, true);
  auto *replacement = context.replacement = body();
  context.replaceOnSync = true;
  click(button(third, TR("Set clock")));
  check(context.writes == 1, "Reentrant keyboard sync submitted a replacement clock field");
  lv_obj_del(third);
  context.current = replacement;
  click(zoneButton(replacement));
  auto *choice = button(lv_layer_top(), settings.zoneLabel(2));
  auto *last = context.replacement = body();
  context.replaceOnClose = true;
  click(choice);
  check(settings.read().zone == 1, "Retired timezone selection changed a reentrant replacement");
  pump(40);
  lv_obj_del(replacement);
  lv_obj_del(last);
  auto *retained = body();
  {
    Screen localScreen(settings, host);
    localScreen.build(retained, 202, false);
  }
  const auto alerts = context.alerts;
  click(button(retained, TR("Set clock")));
  check(context.alerts == alerts && context.writes == 1, "Clock control retained a destroyed owner");
  lv_obj_del(retained);
  screen.detach();
  closingContext = nullptr;
  touchPrefsSetTimeOffsetHours(original.offset);
  settings.setZone(original.zone);
  settings.setFlag(Settings::Flag::Hour12, original.hour12);
  settings.setFlag(Settings::Flag::BootWifi, original.bootWifi);
  settings.setFlag(Settings::Flag::OpenWifi, original.openWifi);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Clock settings leaked a tree");
  puts("Clock settings: calendar/floor validation, real preferences, keyboard snapshots, picker/field "
       "lifetime and reentrancy passed.");
}
