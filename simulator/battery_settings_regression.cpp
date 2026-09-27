// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "platform/UiPlatform.h"
#include "screens/BatterySettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Settings = ui::BatterySettings;
using State = Settings::CalibrationState;
using Screen = ui::screens::BatterySettingsScreen;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
lv_obj_t *find(lv_obj_t *root, const lv_obj_class_t *type, const char *text = nullptr) {
  if (lv_obj_check_type(root, type) && (!text || strstr(lv_label_get_text(root), text)))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = find(lv_obj_get_child(root, i), type, text))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *caption = find(root, &lv_label_class, text);
  check(caption, "Battery settings button missing");
  return lv_obj_get_parent(caption);
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}
void finish(Settings &settings, uint32_t start) {
  for (unsigned i = 0; i < 8; ++i)
    settings.tick(start + i * 20u);
}
struct Context {
  uint16_t raw = 4000, values[8]{};
  unsigned reads = 0, index = 0, sleepChanges = 0, alerts = 0, history = 0;
  int soc = -1;
  bool sequence = false, sleep = false, resetOnRead = false, replaceOnRead = false;
  bool tickOnRead = false, replaceOnSleep = false, replaceOnBlocker = false;
  Settings *settings = nullptr;
  Screen *screen = nullptr;
  lv_obj_t *replacement = nullptr;
  char alert[256]{};
};
Settings::Host host(Context &context, bool smooth = false, bool sleep = true) {
  return {&context,
          [](void *p) -> uint16_t {
            auto &c = *static_cast<Context *>(p);
            ++c.reads;
            if (c.resetOnRead) {
              c.resetOnRead = false;
              c.settings->resetCalibration();
            }
            if (c.tickOnRead) {
              c.tickOnRead = false;
              c.settings->tick(900000);
            }
            if (c.replaceOnRead) {
              c.replaceOnRead = false;
              c.screen->build(c.replacement, 202);
            }
            return c.sequence ? c.values[c.index++ % 8] : c.raw;
          },
          [](void *p) { return static_cast<Context *>(p)->soc; },
          [](void *p, bool on) {
            auto &c = *static_cast<Context *>(p);
            ++c.sleepChanges;
            c.sleep = on;
            if (c.replaceOnSleep) {
              c.replaceOnSleep = false;
              c.screen->build(c.replacement, 202);
            }
          },
          [](void *p, char *out, size_t capacity) {
            auto &c = *static_cast<Context *>(p);
            snprintf(out, capacity, "Test sleep blocker");
            if (c.replaceOnBlocker) {
              c.replaceOnBlocker = false;
              c.screen->build(c.replacement, 202);
            }
          },
          smooth,
          sleep};
}
void serviceRegression() {
  Context context;
  touchPrefsSetBattFullMv(4300);
  Settings settings(host(context));
  context.settings = &settings;
  check(settings.fullMv() == 4300 && settings.percent(3800) == 50 && settings.percent(0) == -1,
        "Battery service lost preference/voltage conversion");
  check(!settings.isCharging(4349) && settings.isCharging(4350), "Battery charge threshold changed");
  context.soc = 12;
  check(settings.percent(4400) == 12, "Hardware fuel gauge did not override voltage");
  context.soc = 101;
  check(settings.percent(3800) == 50, "Invalid hardware fuel gauge did not fall back");
  context.soc = -1;
  check(settings.publishedMv(0xfffffff0u) == 4000, "Battery first publish failed");
  context.raw = 3900;
  check(settings.publishedMv(10) == 4000 && settings.publishedMv(20000) == 3900,
        "Battery publish cache/wrapped deadline changed");
  context.raw = 4400;
  check(settings.publishedMv(20001) == 4400, "Charge transition did not publish immediately");
  context.raw = 0;
  check(settings.publishedMv(20002) == 0, "Missing battery reading returned cached voltage");
  const auto initialReads = context.reads;
  const auto request = settings.beginCalibration(0xfffffff0u);
  check(context.reads == initialReads, "Calibration start synchronously read the ADC");
  context.sequence = true;
  const uint16_t values[8] = {0, 4100, 4200, 0, 4300, 4200, 4100, 4300};
  memcpy(context.values, values, sizeof values);
  settings.tick(0xffffffefu);
  check(context.reads == initialReads, "Calibration sampled before its deadline");
  settings.tick(0xfffffff0u);
  settings.tick(0xfffffff0u);
  settings.tick(3);
  check(context.reads == initialReads + 1, "Calibration oversampled before wrapped deadline");
  for (unsigned i = 1; i < 8; ++i)
    settings.tick(0xfffffff0u + i * 20u);
  check(settings.calibration().id == request && settings.calibration().state == State::Calibrated &&
            settings.fullMv() == 4200 && touchPrefsGetBattFullMv() == 4200,
        "Calibration did not average only nonzero samples and persist");
  settings.tick(100000);
  check(context.reads == initialReads + 8, "Completed calibration kept reading");
  context.sequence = false;
  context.raw = 3499;
  settings.beginCalibration(1000);
  finish(settings, 1000);
  check(settings.calibration().state == State::TooLow && settings.fullMv() == 4200,
        "Low calibration replaced the saved reference");
  context.raw = 0;
  settings.beginCalibration(2000);
  finish(settings, 2000);
  check(settings.calibration().state == State::TooLow, "Empty calibration burst accepted");
  context.raw = 65535;
  settings.beginCalibration(3000);
  settings.tick(3000);
  const auto reads = context.reads;
  settings.tick(800000);
  check(context.reads == reads + 1 && settings.calibration().state == State::Sampling,
        "Late calibration tick burst multiple ADC samples");
  finish(settings, 800020);
  check(settings.fullMv() == 65535, "Calibration accumulator overflowed");
  const auto oldRequest = settings.beginCalibration(4000);
  const auto newRequest = settings.beginCalibration(4000);
  settings.cancelCalibration(oldRequest);
  check(settings.calibration().id == newRequest && settings.calibration().state == State::Sampling,
        "Retired calibration cancelled its replacement");
  settings.cancelCalibration(newRequest);
  const auto stoppedReads = context.reads;
  settings.tick(1000000);
  check(context.reads == stoppedReads, "Cancelled calibration still read ADC");
  settings.beginCalibration(5000);
  context.resetOnRead = true;
  finish(settings, 5000);
  check(settings.calibration().state == State::Reset && settings.fullMv() == 4200 &&
            touchPrefsGetBattFullMv() == 0,
        "Reentrant reset was overwritten by a stale sample");
  context.raw = 3500;
  settings.beginCalibration(6000);
  context.tickOnRead = true;
  finish(settings, 6000);
  check(settings.fullMv() == 3500 && settings.calibration().state == State::Calibrated,
        "Recursive sample tick corrupted calibration");
  context.raw = 4000;
  Settings smooth(host(context, true));
  check(smooth.publishedMv(0) == 4000, "EMA initial sample changed");
  context.raw = 4020;
  check(smooth.publishedMv(20000) == 4003, "Board-specific EMA was lost");
  context.raw = 4200;
  check(smooth.publishedMv(40000) == 4200, "EMA large-step reset changed");
  Settings unsupported(host(context, false, false));
  const auto changes = context.sleepChanges;
  check(!unsupported.sleepState().supported && !unsupported.setSleep(true) && context.sleepChanges == changes,
        "Unsupported sleep capability reached hardware");
}
} // namespace
void runBatterySettingsRegression() {
  const auto originalFull = touchPrefsGetBattFullMv();
  const auto originalSleep = touchPrefsGetSleepIdle();
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  serviceRegression();
  touchPrefsSetBattFullMv(0);
  touchPrefsSetSleepIdle(false);
  Context context;
  Settings settings(host(context));
  context.settings = &settings;
  Screen::Host callbacks{&context, [](void *p) { ++static_cast<Context *>(p)->history; },
                         [](void *p, const char *text, int) {
                           auto &c = *static_cast<Context *>(p);
                           ++c.alerts;
                           snprintf(c.alert, sizeof c.alert, "%s", text);
                         }};
  Screen screen(settings, callbacks);
  context.screen = &screen;
  auto *first = body();
  screen.build(first, 202);
  auto *toggle = find(first, &lv_switch_class);
  auto *calibrate = button(first, TR("Calibrate battery (full = 100%)"));
  auto *history = button(first, TR("Battery & power history"));
  check(toggle && !lv_obj_has_state(toggle, LV_STATE_CHECKED) &&
            find(first, &lv_label_class, "Test sleep blocker"),
        "Battery form lost sleep snapshot");
  lv_obj_update_layout(first);
  for (uint32_t i = 1; i < lv_obj_get_child_cnt(first); ++i) {
    auto *previous = lv_obj_get_child(first, i - 1), *current = lv_obj_get_child(first, i);
    check(lv_obj_get_y(current) >= lv_obj_get_y(previous) + lv_obj_get_height(previous),
          "Battery settings controls overlap");
  }
  lv_event_send(history, LV_EVENT_CLICKED, nullptr);
  check(context.history == 1, "Battery history action missing");
  lv_obj_add_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(context.sleep && touchPrefsGetSleepIdle(), "Sleep toggle did not update hardware and preference");
  const auto alerts = context.alerts;
  lv_event_send(calibrate, LV_EVENT_CLICKED, nullptr);
  check(settings.calibration().state == State::Idle, "Generic click also calibrated after long-press");
  lv_event_send(calibrate, LV_EVENT_SHORT_CLICKED, nullptr);
  check(!context.reads && settings.calibration().state == State::Sampling,
        "Calibration button blocked on ADC reads");
  finish(settings, ui::platform::milliseconds());
  screen.refresh();
  screen.refresh();
  check(settings.fullMv() == 4000 && context.alerts == alerts + 1,
        "Calibration completion missing or notified twice");
  lv_event_send(calibrate, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_event_send(calibrate, LV_EVENT_LONG_PRESSED, nullptr);
  finish(settings, ui::platform::milliseconds());
  screen.refresh();
  check(settings.fullMv() == 4200 && !strcmp(context.alert, TR("Battery calibration reset to default")),
        "Long-press failed to cancel sampling and restore default");
  auto *second = body();
  lv_event_send(calibrate, LV_EVENT_SHORT_CLICKED, nullptr);
  screen.build(second, 202);
  const auto afterReplacement = context.alerts;
  lv_event_send(calibrate, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_event_send(history, LV_EVENT_CLICKED, nullptr);
  lv_obj_clear_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  finish(settings, ui::platform::milliseconds());
  screen.refresh();
  check(settings.fullMv() == 4200 && context.alerts == afterReplacement && context.history == 1 &&
            context.sleep,
        "Retired battery form changed replacement state");
  lv_obj_del(first);
  const auto count = lv_obj_get_child_cnt(second);
  screen.build(second, 202);
  check(lv_obj_get_child_cnt(second) == count, "Battery form rebuild accumulated controls");
  calibrate = button(second, TR("Calibrate battery (full = 100%)"));
  lv_event_send(calibrate, LV_EVENT_SHORT_CLICKED, nullptr);
  auto *third = context.replacement = body();
  context.replaceOnRead = true;
  finish(settings, ui::platform::milliseconds());
  screen.refresh();
  check(settings.fullMv() == 4200 && settings.calibration().state == State::Idle,
        "ADC callback rebind allowed retired calibration to commit");
  lv_obj_del(second);
  auto *fourth = context.replacement = body();
  toggle = find(third, &lv_switch_class);
  context.replaceOnSleep = true;
  const auto beforeReentry = context.alerts;
  lv_obj_clear_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(context.alerts == beforeReentry &&
            !lv_obj_has_state(find(fourth, &lv_switch_class), LV_STATE_CHECKED),
        "Sleep callback wrote into replacement form");
  lv_obj_del(third);
  auto *fifth = context.replacement = body();
  context.replaceOnBlocker = true;
  screen.build(fourth, 202);
  check(lv_obj_get_child_cnt(fourth) == 0 && find(fifth, &lv_switch_class),
        "Reentrant sleep snapshot populated a retired body");
  lv_obj_del(fourth);
  int deletes = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        fifth, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deletes);
  lv_event_send(button(fifth, TR("Calibrate battery (full = 100%)")), LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_del(fifth);
  screen.refresh();
  check(deletes == 3 && settings.calibration().state == State::Idle,
        "Battery DELETE skipped observers or retained its calibration");
  auto *retained = body();
  {
    Screen temporary(settings, callbacks);
    temporary.build(retained, 202);
    lv_event_send(button(retained, TR("Calibrate battery (full = 100%)")), LV_EVENT_SHORT_CLICKED, nullptr);
  }
  const auto beforeStale = context.alerts;
  lv_event_send(button(retained, TR("Calibrate battery (full = 100%)")), LV_EVENT_LONG_PRESSED, nullptr);
  check(context.alerts == beforeStale && settings.calibration().state == State::Idle,
        "Battery control retained a destroyed owner");
  lv_obj_del(retained);
  Settings unsupported(host(context, false, false));
  Screen noSleep(unsupported, callbacks);
  auto *last = body();
  noSleep.build(last, 202);
  check(!find(last, &lv_switch_class) && button(last, TR("Calibrate battery (full = 100%)")),
        "Unsupported board exposes sleep or lost calibration");
  lv_obj_del(last);
  touchPrefsSetBattFullMv(originalFull);
  touchPrefsSetSleepIdle(originalSleep);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Battery settings leaked roots");
  puts("Battery settings regression passed: nonblocking calibration, preferences, board policy and "
       "lifetimes.");
}
