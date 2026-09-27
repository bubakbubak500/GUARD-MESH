// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "platform/UiPlatform.h"
#include "screens/KeyboardSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Settings = ui::KeyboardSettings;
using Screen = ui::screens::KeyboardSettingsScreen;
using Result = ui::KeyBindings::Result;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
lv_obj_t *find(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = find(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  auto *caption = find(root, text);
  if (!caption)
    throw std::runtime_error(std::string("Keyboard button missing: ") + text);
  return lv_obj_get_parent(caption);
}
lv_obj_t *toggle(lv_obj_t *root, const char *text) {
  auto *row = button(root, text);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *object = lv_obj_get_child(row, i);
    if (lv_obj_check_type(object, &lv_switch_class))
      return object;
  }
  throw std::runtime_error("Keyboard toggle missing switch");
}
lv_obj_t *slider(lv_obj_t *root) {
  if (lv_obj_check_type(root, &lv_slider_class))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = slider(lv_obj_get_child(root, i)))
      return found;
  return nullptr;
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
void change(lv_obj_t *object, bool on) {
  if (on)
    lv_obj_add_state(object, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(object, LV_STATE_CHECKED);
  lv_event_send(object, LV_EVENT_VALUE_CHANGED, nullptr);
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}
struct Context {
  Screen *screen = nullptr;
  lv_obj_t *replacement = nullptr;
  unsigned flags = 0, layouts = 0, keys = 0, lights = 0, alerts = 0, applied = 0;
  bool replace = false;
  void changed() {
    if (replace) {
      replace = false;
      screen->build(replacement, 202);
    }
  }
};
Settings::Host host(Context &c,
                    Settings::Capabilities caps = {Settings::Light::Brightness, Settings::Navigation::All,
                                                   true, true, true, true}) {
  return {&c,
          [](void *p, Settings::Flag, bool) {
            auto &c = *static_cast<Context *>(p);
            ++c.flags;
            c.changed();
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.layouts;
            c.changed();
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.keys;
            c.changed();
          },
          [](void *p, uint8_t, uint8_t) {
            auto &c = *static_cast<Context *>(p);
            ++c.lights;
            c.changed();
          },
          caps};
}
Screen::Host formHost(Context &c) {
  return {&c, [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
          [](void *p, Settings::Flag, bool, lv_obj_t *) { ++static_cast<Context *>(p)->applied; }};
}
// Restore actual shared preferences and layout registry, including unset keys.
struct Restore {
  Context context;
  Settings reader{host(context)};
  Settings::State state = reader.read();
  uint8_t keys[ui::KeyBindings::Count]{};
  uint8_t mode = touchPrefsGetKbBacklight(), brightness = touchPrefsGetKbdBacklight();
  uint8_t layout = touchPrefsGetKeyboardLayout();
  uint32_t mask = keyboardLayoutsGetEnabledMask();
  KeyboardLayoutId current = keyboardLayoutsGetCurrent();
  Restore() {
    for (unsigned i = 0; i < ui::KeyBindings::Count; ++i)
      keys[i] = i < 5 ? touchPrefsGetNavKey(i) : touchPrefsGetNavDirKey(i - 5);
  }
  ~Restore() {
    for (unsigned i = 0; i < Settings::FlagCount; ++i)
      reader.setFlag(static_cast<Settings::Flag>(i), state.flags[i]);
    for (unsigned i = 0; i < ui::KeyBindings::Count; ++i)
      if (i < 5)
        touchPrefsSetNavKey(i, keys[i]);
      else
        touchPrefsSetNavDirKey(i - 5, keys[i]);
    touchPrefsSetKbBacklight(mode);
    touchPrefsSetKbdBacklight(brightness);
    touchPrefsSetEnabledLayouts(state.layouts);
    touchPrefsSetKeyboardLayout(layout);
    keyboardLayoutsSetEnabledMask(mask);
    keyboardLayoutsApply(nullptr, current);
  }
};
void serviceRegression() {
  for (unsigned i = 0; i < 5; ++i)
    touchPrefsSetNavKey(i, 0);
  for (unsigned i = 0; i < 8; ++i)
    touchPrefsSetNavDirKey(i, 0);
  touchPrefsSetKbBacklight(0);
  touchPrefsSetKbdBacklight(50);
  Context c;
  Settings settings(host(c));
  check(settings.key(0) == 'e' && settings.directionFor('W') == 0, "Default bindings not loaded");
  check(settings.assign(0, 'W') == Result::Duplicate && settings.assign(0, 27) == Result::Cancelled &&
            settings.assign(0, '1') == Result::Invalid && c.keys == 0,
        "Rejected key mutated settings");
  check(settings.assign(0, 'B') == Result::Ok && settings.tabFor('B') == 0 && touchPrefsGetNavKey(0) == 'b',
        "Binding cache/persistence mismatch");
  check(settings.assign(13, 'b') == Result::OutOfRange, "Out of range binding accepted");
  check(!settings.setLayout(0, false) && !settings.setLayout(KEYBOARD_LAYOUT_COUNT, true),
        "Invalid layout accepted");
  check(settings.setLayout(7, true), "Layout enable failed");
  keyboardLayoutsApply(nullptr, KeyboardLayoutId::FR);
  check(keyboardLayoutsGetCurrent() == KeyboardLayoutId::FR, "Layout setup failed");
  check(settings.setLayout(7, false) && keyboardLayoutsGetCurrent() == KeyboardLayoutId::EN &&
            touchPrefsGetKeyboardLayout() == 0,
        "Disabling active layout failed to return to EN");
  check(c.layouts == 2, "Invalid layout notified host");
  settings.setBrightness(25, 100);
  check(settings.lightMode() == 1 && settings.lightLevel() == 16 && settings.lightSavePending(),
        "Brightness did not turn on light");
  settings.tick(1299);
  check(touchPrefsGetKbdBacklight() == 50 && touchPrefsGetKbBacklight() == 0,
        "Backlight saved before debounce");
  settings.setBrightness(75, 1200);
  settings.tick(1300);
  check(touchPrefsGetKbdBacklight() == 50, "Repeated adjustment did not extend debounce");
  settings.tick(2400);
  check(touchPrefsGetKbdBacklight() == 75 && !settings.lightSavePending(),
        "Backlight did not save at deadline");
  touchPrefsSetKbdBacklight(60);
  settings.tick(3000);
  check(touchPrefsGetKbdBacklight() == 60, "Completed debounce wrote again");
  settings.lightOff(0xfffffff0u);
  settings.tick(1183);
  check(settings.lightSavePending() && settings.brightness() == 75,
        "Off lost brightness or wrap deadline fired early");
  settings.tick(1184);
  check(!settings.lightSavePending() && touchPrefsGetKbBacklight() == 0, "Wrapped deadline not saved");
  settings.setBrightness(-3, 4000);
  check(settings.brightness() == 1, "Minimum brightness not enforced");
  settings.setBrightness(200, 4001);
  check(settings.brightness() == 100, "Maximum brightness not enforced");
  settings.cycleLight(4002);
  check(settings.lightMode() == 2, "Auto mode skipped");
  settings.cycleLight(4003);
  check(settings.lightMode() == 0, "Mode cycle did not wrap");
  settings.saveLight();
}
void screenRegression(void (*pump)(unsigned)) {
  touchPrefsSetKbForceLegacy(false);
  Context c;
  Settings settings(host(c));
  Screen screen(settings, formHost(c));
  c.screen = &screen;
  auto *first = body();
  screen.build(first, 202);
  auto *light = slider(first);
  check(light, "Brightness control missing");
  lv_slider_set_value(light, 42, LV_ANIM_OFF);
  lv_event_send(light, LV_EVENT_VALUE_CHANGED, nullptr);
  check(settings.brightness() == 42 && settings.lightSavePending(), "Keyboard slider change not scheduled");
  change(toggle(first, TR("Older keyboard protocol")), true);
  check(lv_obj_has_flag(lv_obj_get_parent(light), LV_OBJ_FLAG_HIDDEN),
        "Legacy protocol left light controls visible");
  change(toggle(first, TR("Older keyboard protocol")), false);
  check(!lv_obj_has_flag(lv_obj_get_parent(light), LV_OBJ_FLAG_HIDDEN), "Light controls did not return");
  click(button(first, TR("Messages")));
  check(screen.capturing(), "Binding click did not capture");
  screen.detach();
  check(!screen.captureKey('j') && !screen.capturing(), "Detached form consumed key");
  const auto lights = c.lights;
  lv_event_send(light, LV_EVENT_VALUE_CHANGED, nullptr);
  click(button(first, TR("Messages")));
  check(c.lights == lights && !screen.capturing(), "Detached controls retained callbacks");
  check(settings.lightSavePending(), "Closing form discarded save");
  settings.tick(ui::platform::milliseconds() + 1200);
  check(touchPrefsGetKbdBacklight() == 42, "Slider without RELEASE did not persist after close");
  lv_obj_del(first);
  auto *second = body();
  screen.build(second, 202);
  click(button(second, "Off"));
  click(button(second, "25"));
  check(settings.brightness() == 25 && settings.lightMode() == 1 && lv_slider_get_value(slider(second)) == 25,
        "Presets/cache/widget disagree");
  click(button(second, TR("Messages")));
  check(screen.captureKey('j'), "Captured key rejected");
  check(settings.key(0) == 'j' && !screen.capturing(), "Captured key not committed");
  auto *third = c.replacement = body();
  c.replace = true;
  const auto notices = c.alerts, applied = c.applied;
  change(toggle(second, TR("Accent popups")), false);
  check(c.alerts == notices && c.applied == applied, "Retired flag event continued into replacement");
  lv_obj_del(second);
  auto *fourth = c.replacement = body();
  c.replace = true;
  change(toggle(third, "French"), true);
  check(c.alerts == notices, "Retired layout event displayed toast");
  lv_obj_del(third);
  auto *fifth = c.replacement = body();
  click(button(fourth, TR("Messages")));
  const auto keyNotices = c.alerts;
  c.replace = true;
  check(screen.captureKey('k'), "Reentrant capture not handled");
  check(c.alerts == keyNotices && !screen.capturing(), "Retired capture continued into replacement");
  lv_obj_del(fourth);
  auto *sixth = c.replacement = body();
  c.replace = true;
  click(button(fifth, "50"));
  lv_obj_del(fifth);
  check(settings.brightness() == 50 && lv_slider_get_value(slider(sixth)) == 50,
        "Reentrant light update lost state");
  click(button(sixth, TR("Messages")));
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        sixth, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(sixth);
  check(deleted == 3 && !screen.capturing(), "DELETE skipped observers or retained capture");
  auto *retained = body();
  {
    Screen temporary(settings, formHost(c));
    temporary.build(retained, 202);
  }
  const auto flags = c.flags;
  change(toggle(retained, TR("Accent popups")), true);
  check(c.flags == flags, "Destroyed form retained callback");
  lv_obj_del(retained);
  settings.saveLight();
  pump(2);
}
void capabilitiesRegression() {
  Context c;
  Settings extra(
      host(c, {Settings::Light::None, Settings::Navigation::ExtraDirections, true, false, false, false}));
  Screen screen(extra, formHost(c));
  auto *root = body();
  screen.build(root, 202);
  check(!find(root, TR("Messages")) && !find(root, TR("Back")) && find(root, TR("Scroll down")) &&
            !slider(root),
        "Extra direction capabilities leaked controls");
  check(extra.assign(0, 'h') == Result::OutOfRange && extra.assign(10, 'h') == Result::OutOfRange &&
            !extra.setFlag(Settings::KeyboardNav, true),
        "Unsupported settings accepted");
  lv_obj_del(root);
  Settings pager(host(c, {Settings::Light::Mode, Settings::Navigation::None, false, false, false, false}));
  Screen mode(pager, formHost(c));
  root = body();
  mode.build(root, 202);
  const auto initial = pager.lightMode();
  const char *names[] = {"Off", "On", "Auto"};
  click(button(root, names[initial]));
  check(pager.lightMode() == (initial + 1) % 3 && !pager.lightSavePending() && !slider(root),
        "Mode-only control failed");
  check(!find(root, TR("Enter key sends message")) && !find(root, TR("Keyboard navigation")),
        "Mode-only device exposed physical keyboard flags");
  lv_obj_del(root);
}
} // namespace
void runKeyboardSettingsRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Restore restore;
  serviceRegression();
  screenRegression(pump);
  capabilitiesRegression();
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Keyboard settings leaked roots");
  puts("Keyboard settings: bindings, layouts, debounce/wrap, capabilities, reentrant callbacks and lifetimes "
       "passed.");
}
