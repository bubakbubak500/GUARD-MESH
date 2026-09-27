// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/DisplaySettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Settings = ui::DisplaySettings;
using Screen = ui::screens::DisplaySettingsScreen;
using Result = Settings::Result;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
lv_obj_t *find(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = find(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *typed(lv_obj_t *root, const lv_obj_class_t *type) {
  if (lv_obj_check_type(root, type))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = typed(lv_obj_get_child(root, i), type))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  auto *caption = find(root, text);
  if (!caption)
    throw std::runtime_error(std::string("Display control missing: ") + text);
  return lv_obj_get_parent(caption);
}
lv_obj_t *toggle(lv_obj_t *root, const char *text) {
  auto *found = typed(button(root, text), &lv_switch_class);
  check(found, "Display switch missing");
  return found;
}
void change(lv_obj_t *object, bool on) {
  if (on)
    lv_obj_add_state(object, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(object, LV_STATE_CHECKED);
  lv_event_send(object, LV_EVENT_VALUE_CHANGED, nullptr);
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
  lv_obj_t *replacement = nullptr;
  uint16_t timeout = 60;
  unsigned reads = 0, writes = 0, changed = 0, alerts = 0, restarts = 0, pickers = 0;
  bool messageLed = false;
  bool fail = false, replaceRead = false, replaceWrite = false, replaceSync = false, replaceAttach = false,
       replaceChange = false;
  Settings::Restart last = Settings::Restart::Night;
  void replace(bool &pending) {
    if (pending) {
      pending = false;
      screen->build(replacement, 202);
    }
  }
};
Settings::Host host(Context &c,
                    Settings::Capabilities caps = {true, false, true, true, true, true, true, true}) {
  return {&c,
          [](void *p) -> uint16_t {
            auto &c = *static_cast<Context *>(p);
            ++c.reads;
            c.replace(c.replaceRead);
            return c.timeout;
          },
          [](void *p, uint16_t seconds) {
            auto &c = *static_cast<Context *>(p);
            ++c.writes;
            if (!c.fail)
              c.timeout = seconds;
            c.replace(c.replaceWrite);
            return !c.fail;
          },
          [](void *p, Settings::Flag, bool) {
            auto &c = *static_cast<Context *>(p);
            ++c.changed;
            c.replace(c.replaceChange);
          },
          [](void *p, Settings::Restart reason) {
            auto &c = *static_cast<Context *>(p);
            ++c.restarts;
            c.last = reason;
          },
          [](void *p) { return static_cast<Context *>(p)->messageLed; },
          [](void *p, bool on) { static_cast<Context *>(p)->messageLed = on; },
          caps};
}
Screen::Host formHost(Context &c) {
  return {&c,
          [](void *p, lv_obj_t *) {
            auto &c = *static_cast<Context *>(p);
            c.replace(c.replaceAttach);
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            c.replace(c.replaceSync);
          },
          [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
          [](void *p) { ++static_cast<Context *>(p)->pickers; },
          nullptr,
          nullptr};
}
struct Restore {
  Context context;
  Settings settings{host(context)};
  Settings::State state = settings.read();
  ~Restore() {
    for (unsigned i = 0; i < Settings::FlagCount; ++i)
      settings.setFlag(static_cast<Settings::Flag>(i), state.flags[i]);
    touchPrefsSetUiScale(state.size);
    touchPrefsSetThemeMode(state.theme);
    touchPrefsSetUiRotation(state.rotation);
  }
};
void serviceRegression() {
  uint16_t seconds = 777;
  const char *invalid[] = {nullptr, "", "-", "12x", "1.5", " 5", "1 0"};
  for (auto *text : invalid)
    check(!Settings::parseTimeout(text, seconds) && seconds == 777, "Invalid timeout changed output");
  check(Settings::parseTimeout("0", seconds) && seconds == 0, "Never timeout rejected");
  check(Settings::parseTimeout("5", seconds) && seconds == 10, "Timeout minimum changed");
  check(Settings::parseTimeout("999999999999999999999", seconds) && seconds == 3600,
        "Timeout overflow not clamped");
  check(Settings::parseTimeout("-12", seconds) && seconds == 0, "Negative timeout not clamped");
  Context c;
  Settings settings(host(c));
  check(settings.setTimeout("12", seconds) == Result::Applied && c.timeout == 12, "Timeout host not updated");
  c.fail = true;
  check(settings.setTimeout("30", seconds) == Result::Failed && c.timeout == 12,
        "Failed timeout reported success");
  check(settings.setTimeout("", seconds) == Result::Invalid && c.writes == 2, "Invalid timeout reached host");
  check(settings.setSize(2) == Result::Applied && touchPrefsGetUiScale() == 2 &&
            settings.setSize(3) == Result::Invalid,
        "UI size bounds/persistence invalid");
  touchPrefsSetThemeMode(0);
  check(settings.setTheme(0) == Result::Unchanged && c.restarts == 0, "Same theme rebooted");
  check(settings.setTheme(1) == Result::Applied && c.last == Settings::Restart::Day &&
            touchPrefsGetThemeMode() == 1,
        "Day theme not saved before restart");
  check(settings.setTheme(2) == Result::Invalid && c.restarts == 1, "Invalid theme rebooted");
  touchPrefsSetUiRotation(3);
  check(settings.rotate() == Result::Applied && touchPrefsGetUiRotation() == 0,
        "Flipped orientation did not return to portrait");
  check(settings.rotate() == Result::Applied && touchPrefsGetUiRotation() == 1 &&
            c.last == Settings::Restart::Rotation,
        "Rotation restart failed");
  for (unsigned i = 0; i < Settings::FlagCount; ++i) {
    const auto flag = static_cast<Settings::Flag>(i);
    check(settings.setFlag(flag, true) && settings.read().flags[i], "Display flag on did not persist");
    check(settings.setFlag(flag, false) && !settings.read().flags[i], "Display flag off did not persist");
  }
  Settings limited(host(c, {false, false, false, false, false, false, false, false}));
  check(limited.setSize(0) == Result::Unsupported && limited.setTheme(0) == Result::Unsupported &&
            limited.rotate() == Result::Unsupported && !limited.setFlag(Settings::GlanceLocked, true),
        "Unsupported display action accepted");
}
void screenRegression(void (*pump)(unsigned)) {
  touchPrefsSetGlanceEnabled(true);
  touchPrefsSetGlanceWhenLocked(true);
  Context c;
  Settings settings(host(c));
  Screen screen(settings, formHost(c));
  c.screen = &screen;
  auto *first = body();
  screen.build(first, 202);
  auto *locked = toggle(first, TR("At a glance while locked"));
  change(toggle(first, TR("At a glance")), false);
  check(lv_obj_has_state(locked, LV_STATE_DISABLED) && touchPrefsGetGlanceWhenLocked(),
        "Master disabled erased locked-preview preference");
  change(locked, false);
  check(touchPrefsGetGlanceWhenLocked(), "Disabled control changed privacy preference");
  // The disabled synthetic event above changed the widget state only; a user
  // cannot toggle it. Rebuilding must restore the persisted preference.
  screen.build(first, 202);
  locked = toggle(first, TR("At a glance while locked"));
  change(toggle(first, TR("At a glance")), true);
  check(!lv_obj_has_state(locked, LV_STATE_DISABLED) && lv_obj_has_state(locked, LV_STATE_CHECKED),
        "Master enable did not preserve locked preview");
  auto *field = typed(first, &lv_textarea_class);
  lv_textarea_set_text(field, "5");
  lv_event_send(field, LV_EVENT_DEFOCUSED, nullptr);
  check(c.timeout == 10 && !strcmp(lv_textarea_get_text(field), "10"), "Timeout form not normalized");
  const auto writes = c.writes;
  lv_textarea_set_text(field, "");
  lv_event_send(field, LV_EVENT_DEFOCUSED, nullptr);
  check(c.writes == writes && c.timeout == 10, "Empty timeout silently disabled timeout");
  auto *size = typed(first, &lv_dropdown_class);
  lv_dropdown_open(size);
  screen.detach();
  check(!lv_dropdown_is_open(size), "Detached form retained dropdown overlay");
  const auto changes = c.changed;
  change(toggle(first, TR("Distance in miles")), true);
  lv_event_send(field, LV_EVENT_DEFOCUSED, nullptr);
  check(c.changed == changes && c.writes == writes, "Detached display callbacks remained active");
  lv_obj_del(first);
  auto *second = body();
  auto *third = c.replacement = body();
  c.replaceRead = true;
  screen.build(second, 202);
  check(lv_obj_get_child_cnt(second) == 0 && typed(third, &lv_textarea_class),
        "Reentrant read built retired form");
  lv_obj_del(second);
  auto *fourth = c.replacement = body();
  c.replaceSync = true;
  field = typed(third, &lv_textarea_class);
  lv_textarea_set_text(field, "88");
  lv_event_send(field, LV_EVENT_DEFOCUSED, nullptr);
  check(c.writes == writes && c.timeout == 10, "Retired mirrored draft saved to new form");
  lv_obj_del(third);
  auto *fifth = c.replacement = body();
  c.replaceWrite = true;
  field = typed(fourth, &lv_textarea_class);
  lv_textarea_set_text(field, "33");
  const auto alerts = c.alerts;
  lv_event_send(field, LV_EVENT_DEFOCUSED, nullptr);
  check(c.timeout == 33 && c.alerts == alerts &&
            !strcmp(lv_textarea_get_text(typed(fifth, &lv_textarea_class)), "33"),
        "Reentrant write touched retired form");
  lv_obj_del(fourth);
  auto *sixth = c.replacement = body();
  c.replaceChange = true;
  change(toggle(fifth, TR("Distance in miles")), true);
  check(c.alerts == alerts, "Reentrant flag callback continued toast");
  lv_obj_del(fifth);
  click(button(sixth, TR("Pick colour")));
  check(c.pickers == 1, "Accent picker host not called");
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        sixth, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(sixth);
  check(deleted == 3, "Display DELETE skipped observers");
  auto *seventh = body();
  auto *eighth = c.replacement = body();
  c.replaceAttach = true;
  screen.build(seventh, 202);
  lv_obj_del(seventh);
  check(typed(eighth, &lv_textarea_class) && find(eighth, TR("Appearance")),
        "Field attachment reentry lost replacement");
  lv_obj_del(eighth);
  auto *retained = body();
  {
    Screen temporary(settings, formHost(c));
    temporary.build(retained, 202);
  }
  const auto lastChanges = c.changed;
  change(toggle(retained, TR("Distance in miles")), false);
  check(c.changed == lastChanges, "Destroyed display form retained callback");
  lv_obj_del(retained);
  Settings limited(host(c, {false, false, false, false, false, false, false, false}));
  Screen small(limited, formHost(c));
  auto *last = body();
  small.build(last, 202);
  check(!typed(last, &lv_dropdown_class) && !find(last, TR("Appearance")) &&
            !find(last, TR("At a glance while locked")) && find(last, TR("Distance in miles")),
        "Display capabilities not reflected");
  lv_obj_del(last);
  pump(2);
}
} // namespace
void runDisplaySettingsRegression(void (*pump)(unsigned)) {
  Restore restore;
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  serviceRegression();
  screenRegression(pump);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Display settings leaked roots");
  puts("Display settings: timeout validation, real preferences, privacy master, capabilities, restart "
       "routing and reentrant lifetimes passed.");
}
