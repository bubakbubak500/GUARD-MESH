// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/GeneralSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Settings = ui::GeneralSettings;
using Screen = ui::screens::GeneralSettingsScreen;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
lv_obj_t *findText(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findText(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *find(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  return findText(root, text);
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *caption = find(root, text);
  if (!caption)
    throw std::runtime_error(std::string("General control missing: ") + text);
  return lv_obj_get_parent(caption);
}
lv_obj_t *toggle(lv_obj_t *root, const char *text) {
  auto *row = button(root, text);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *child = lv_obj_get_child(row, i);
    if (lv_obj_check_type(child, &lv_switch_class))
      return child;
  }
  throw std::runtime_error("General switch missing");
}
lv_obj_t *dropdown(lv_obj_t *root, unsigned index) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *child = lv_obj_get_child(root, i);
    if (lv_obj_check_type(child, &lv_dropdown_class) && !index--)
      return child;
  }
  throw std::runtime_error("General dropdown missing");
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
void choose(lv_obj_t *object, unsigned value) {
  lv_dropdown_set_selected(object, value);
  lv_event_send(object, LV_EVENT_VALUE_CHANGED, nullptr);
}
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
  lv_obj_t *replacement = nullptr, *history = nullptr;
  Settings::StorageSnapshot storage;
  bool onRead = false, onClose = false, onAction = false, onAdvert = false, chooseOnClose = false;
  bool advertOk = true;
  unsigned actions = 0, adverts = 0, alerts = 0;
  unsigned heardCount = 12, clears = 0;
  Settings::Action last = Settings::Action::Setup;
  void replace(bool &flag) {
    if (flag) {
      flag = false;
      screen->build(replacement, 202);
    }
  }
};
Context *closing = nullptr;
Settings::Host host(Context &c, Settings::Capabilities caps = {true, true, true, false, true}) {
  return {&c,
          [](void *p, Settings::StorageSnapshot &snapshot) {
            auto &c = *static_cast<Context *>(p);
            snapshot = c.storage;
            c.replace(c.onRead);
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.adverts;
            c.replace(c.onAdvert);
            return c.advertOk;
          },
          [](void *p, Settings::Action action) {
            auto &c = *static_cast<Context *>(p);
            ++c.actions;
            c.last = action;
            c.replace(c.onAction);
          },
          caps,
          [](void* p, unsigned& count, unsigned& capacity) { count = static_cast<Context*>(p)->heardCount; capacity = 1024; },
          [](void* p) { auto& c = *static_cast<Context*>(p); ++c.clears; c.heardCount = 0; return true; }};
}
Screen::Host formHost(Context &c) {
  return {&c,
          [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
          nullptr,
          {[]() -> lv_coord_t { return 24; },
           [](lv_obj_t **root) {
             auto *old = *root;
             *root = nullptr;
             lv_obj_del_async(old);
             if (!closing)
               return;
             closing->replace(closing->onClose);
             if (closing->chooseOnClose) {
               closing->chooseOnClose = false;
               choose(closing->history, 0);
             }
           },
           nullptr}};
}
struct Restore {
  uint16_t history = touchPrefsGetHistPerChat();
  uint8_t fallback = touchPrefsGetHistSyncAfter();
  bool sd = touchPrefsGetUseSdStorage(), console = touchPrefsGetConsoleMode();
  ~Restore() {
    touchPrefsSetHistPerChat(history);
    touchPrefsSetHistSyncAfter(fallback);
    touchPrefsSetUseSdStorage(sd);
    touchPrefsSetConsoleMode(console);
  }
};
void serviceRegression() {
  Context c;
  Settings settings(host(c));
  const uint16_t caps[] = {100, 250, 500, 1000, 2000, 0};
  const uint8_t retries[] = {0, 1, 2, 3, 5, 8};
  for (unsigned i = 0; i < 6; ++i) {
    check(settings.setHistory(i) && touchPrefsGetHistPerChat() == caps[i] && settings.read().history == i,
          "History option mapping wrong");
    check(settings.setFallback(i) && touchPrefsGetHistSyncAfter() == retries[i] &&
              settings.read().fallback == i,
          "Fallback option mapping wrong");
  }
  check(!settings.setHistory(6) && !settings.setFallback(6), "Invalid history options accepted");
  touchPrefsSetHistPerChat(1234);
  touchPrefsSetHistSyncAfter(7);
  check(settings.read().history == 1 && settings.read().fallback == 2,
        "Custom history settings lost display fallback");
  using Storage = Settings::Storage;
  Settings::StorageSnapshot snapshot;
  check(Settings::storageStatus(false, snapshot) == Storage::Internal &&
            Settings::storageStatus(true, snapshot) == Storage::MountFailed,
        "Internal storage intent not distinguished");
  snapshot.contactsOnSd = true;
  check(Settings::storageStatus(false, snapshot) == Storage::MissingCard &&
            Settings::storageStatus(true, snapshot) == Storage::MissingFullData,
        "Missing SD status incorrect");
  snapshot.migrationBlocked = true;
  check(Settings::storageStatus(true, snapshot) == Storage::MissingFullData,
        "Migration hid missing active SD");
  snapshot.cardPresent = true;
  check(Settings::storageStatus(true, snapshot) == Storage::MigrationBlocked &&
            Settings::storageStatus(false, snapshot) == Storage::Sd,
        "Migration status ignored selection");
  snapshot.migrationBlocked = false;
  check(Settings::storageStatus(true, snapshot) == Storage::Sd, "Mounted contacts status incorrect");
  // The desktop preferences backend intentionally cannot write the boot-only
  // namespace (SimPrefs::writeFileBool returns false). The real setter still
  // updates the regular preference; the service must propagate that failure.
  check(!settings.setSd(true) && touchPrefsGetUseSdStorage(),
        "SD boot persistence failure hidden or regular preference not updated");
  check(settings.setConsole(true) && c.last == Settings::Action::ConsoleOn && touchPrefsGetConsoleMode(),
        "Console intent/action mismatch");
  check(settings.setConsole(false) && c.last == Settings::Action::ConsoleOff, "Console exit action missing");
  check(!settings.action(Settings::Action::ConsoleOn) && !settings.action(static_cast<Settings::Action>(99)),
        "Action bypassed console persistence");
  Settings limited(host(c, {false, false, false, false, false}));
  const auto actions = c.actions;
  check(!limited.setSd(false) && !limited.setConsole(true) && !limited.action(Settings::Action::Recover) &&
            c.actions == actions,
        "Unsupported general action accepted");
}
void screenRegression(void (*pump)(unsigned)) {
  touchPrefsSetHistPerChat(250);
  touchPrefsSetUseSdStorage(false);
  touchPrefsSetConsoleMode(false);
  Context c;
  Settings settings(host(c));
  Screen screen(settings, formHost(c));
  c.screen = &screen;
  closing = &c;
  auto *first = body();
  screen.build(first, 202);
  click(button(first, TR("Clear heard-name cache")));
  check(screen.confirmationOpen() && c.clears == 0, "Heard-name cache cleared before confirmation");
  click(button(lv_layer_top(), TR("Cancel")));
  pump(2);
  check(c.clears == 0, "Cancelled cache clear changed names");
  click(button(first, TR("Clear heard-name cache")));
  click(button(lv_layer_top(), TR("Clear")));
  check(c.clears == 1 && c.heardCount == 0 && c.actions == 0, "Cache clear changed unrelated settings");
  pump(2);
  auto *history = dropdown(first, 0);
  check(find(first, TR("Contacts are on internal flash.")), "Storage snapshot not rendered");
  choose(history, 5);
  check(screen.confirmationOpen() && touchPrefsGetHistPerChat() == 250 &&
            lv_dropdown_get_selected(history) == 1,
        "Unlimited option committed before confirmation");
  auto *oldConfirm = button(lv_layer_top(), TR("Turn off"));
  click(button(lv_layer_top(), TR("Cancel")));
  click(oldConfirm);
  check(touchPrefsGetHistPerChat() == 250 && !screen.confirmationOpen(),
        "Cancelled confirmation changed history");
  pump(2);
  choose(history, 5);
  click(button(lv_layer_top(), TR("Turn off")));
  check(touchPrefsGetHistPerChat() == 0 && lv_dropdown_get_selected(history) == 5,
        "Accepted unlimited option not saved");
  pump(2);
  choose(history, 1);
  choose(history, 5);
  oldConfirm = button(lv_layer_top(), TR("Turn off"));
  auto *second = body();
  screen.build(second, 202);
  click(oldConfirm);
  check(touchPrefsGetHistPerChat() == 250 && !screen.confirmationOpen(),
        "Old dialog affected replacement form");
  choose(history, 0);
  check(touchPrefsGetHistPerChat() == 250, "Old dropdown retained callback");
  lv_obj_del(first);
  pump(2);
  history = dropdown(second, 0);
  choose(history, 5);
  auto *third = c.replacement = body();
  c.onClose = true;
  click(button(lv_layer_top(), TR("Turn off")));
  check(touchPrefsGetHistPerChat() == 250, "Reentrant confirmation close saved retired action");
  lv_obj_del(second);
  pump(2);
  history = c.history = dropdown(third, 0);
  choose(history, 5);
  c.chooseOnClose = true;
  click(button(lv_layer_top(), TR("Turn off")));
  check(touchPrefsGetHistPerChat() == 100 && lv_dropdown_get_selected(history) == 0,
        "New selection during close was overwritten by old confirmation");
  pump(2);
  click(button(third, TR(LV_SYMBOL_DOWNLOAD "  Copy internal data to SD")));
  check(c.actions == 0, "Recovery ran before confirmation");
  click(button(lv_layer_top(), TR("Copy")));
  check(c.actions == 1 && c.last == Settings::Action::Recover, "Recovery confirmation not dispatched");
  pump(2);
  c.storage.contactsOnSd = true;
  c.storage.cardPresent = false;
  change(toggle(third, TR("Store data on SD (reboot)")), true);
  check(find(third, TR("SD data is unavailable - identity, settings, contacts and channels cannot be saved "
                       "until the card is reinserted.")),
        "Changed storage intent not reflected");
  click(button(third, TR("Reboot device")));
  check(c.actions == 2 && c.last == Settings::Action::Reboot, "Reboot button not dispatched");
  char setup[96];
  snprintf(setup, sizeof setup, LV_SYMBOL_REFRESH "  %s", TR("Run setup again"));
  click(button(third, setup));
  check(c.last == Settings::Action::Setup, "Setup button not dispatched");
  auto *fourth = c.replacement = body();
  c.onAdvert = true;
  const auto alerts = c.alerts;
  click(button(third, TR("Send advert now")));
  check(c.adverts == 1 && c.alerts == alerts, "Reentrant advert displayed stale alert");
  lv_obj_del(third);
  auto *fifth = c.replacement = body();
  c.onAction = true;
  change(toggle(fourth, TR("Text console (experimental)")), true);
  check(c.last == Settings::Action::ConsoleOn && touchPrefsGetConsoleMode(),
        "Console toggle not saved/dispatched");
  lv_obj_del(fourth);
  auto *sixth = c.replacement = body();
  c.onRead = true;
  change(toggle(fifth, TR("Store data on SD (reboot)")), false);
  lv_obj_del(fifth);
  check(find(sixth, TR("Store data on SD (reboot)")), "Storage-read reentry lost replacement");
  choose(dropdown(sixth, 0), 5);
  oldConfirm = button(lv_layer_top(), TR("Turn off"));
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        sixth, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  const auto cap = touchPrefsGetHistPerChat();
  lv_obj_del(sixth);
  click(oldConfirm);
  check(deleted == 3 && !screen.confirmationOpen() && touchPrefsGetHistPerChat() == cap,
        "DELETE retained confirmation or skipped observers");
  pump(2);
  auto *retained = body();
  {
    Screen temporary(settings, formHost(c));
    c.screen = &temporary;
    temporary.build(retained, 202);
    choose(dropdown(retained, 0), 5);
    oldConfirm = button(lv_layer_top(), TR("Turn off"));
  }
  click(oldConfirm);
  choose(dropdown(retained, 0), 5);
  check(touchPrefsGetHistPerChat() == cap, "Destroyed form retained callbacks");
  c.screen = &screen;
  lv_obj_del(retained);
  pump(2);
  Settings pager(host(c, {true, true, true, true, false}));
  Screen resume(pager, formHost(c));
  auto *last = body();
  resume.build(last, 202);
  click(button(last, TR(LV_SYMBOL_DOWNLOAD "  Copy internal data to SD")));
  check(find(lv_layer_top(), TR("Resume")) && !find(last, TR("Text console (experimental)")),
        "Pager recovery/capability labels wrong");
  lv_obj_del(last);
  pump(2);
  closing = nullptr;
}
} // namespace
void runGeneralSettingsRegression(void (*pump)(unsigned)) {
  Restore restore;
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  serviceRegression();
  screenRegression(pump);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "General settings leaked roots");
  puts("General settings: history choices, storage snapshots, owned confirmations, reentrant actions, stale "
       "events and lifetimes passed.");
}
