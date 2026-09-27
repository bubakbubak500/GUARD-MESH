// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/SoundSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Settings = ui::SoundSettings;
using Screen = ui::screens::SoundSettingsScreen;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
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
  // LV_LABEL_LONG_DOT temporarily modifies its text while the new parent has
  // not been laid out. Inspect the same settled labels a frame would display.
  lv_obj_update_layout(root);
  return findText(root, text);
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *caption = find(root, text);
  if (!caption)
    throw std::runtime_error(std::string("Sound button missing: ") + text);
  return lv_obj_get_parent(caption);
}
lv_obj_t *toggle(lv_obj_t *root, const char *text) {
  auto *caption = find(root, text);
  check(caption, "Sound toggle missing");
  auto *row = lv_obj_get_parent(caption);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *child = lv_obj_get_child(row, i);
    if (lv_obj_check_type(child, &lv_switch_class))
      return child;
  }
  throw std::runtime_error("Sound toggle has no switch");
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
  Settings *settings = nullptr;
  Screen *screen = nullptr;
  lv_obj_t *replacement = nullptr;
  bool quiet = false, ioFail = false, replaceOnRead = false, replaceOnChanged = false;
  bool stopOnWrite = false, replaceOnClose = false, replaceOnAlert = false;
  int minute = -1;
  unsigned changed = 0, alerts = 0, files = 0, reads = 0, volume = 0;
  char alert[192]{};
  std::vector<unsigned> played, outputs;
};
Context *closing = nullptr;
Settings::Host host(Context &c, Settings::Capabilities caps = {true, true, true, true, true}) {
  return {&c,
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.reads;
            if (c.replaceOnRead) {
              c.replaceOnRead = false;
              c.screen->build(c.replacement, 202);
            }
            return c.quiet;
          },
          [](void *p, bool quiet) { static_cast<Context *>(p)->quiet = quiet; },
          [](void *p) { return static_cast<Context *>(p)->minute; },
          [](void *p, unsigned slot) { static_cast<Context *>(p)->played.push_back(slot); },
          [](void *p, uint8_t volume) { static_cast<Context *>(p)->volume = volume; },
          [](void *p, uint8_t mask) {
            auto &c = *static_cast<Context *>(p);
            c.outputs.push_back(mask);
            if (c.stopOnWrite) {
              c.stopOnWrite = false;
              c.settings->setFlag(Settings::Indicator, false, 500);
            }
            return !c.ioFail;
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.changed;
            if (c.replaceOnChanged) {
              c.replaceOnChanged = false;
              c.screen->build(c.replacement, 202);
            }
          },
          caps};
}
void serviceRegression() {
  Context c;
  Settings settings(host(c));
  c.settings = &settings;
  touchPrefsSetDndEnabled(true);
  touchPrefsSetDndStartSlot(44);
  touchPrefsSetDndEndSlot(12);
  check(!settings.dndActive(), "Unknown clock enabled DND");
  c.minute = 1380;
  check(settings.dndActive() && settings.preview(0) == Settings::Preview::DoNotDisturb && c.played.empty(),
        "DND did not silence preview");
  c.quiet = true;
  c.minute = 600;
  check(settings.preview(1) == Settings::Preview::Quiet, "Master mute did not silence preview");
  settings.setFlag(Settings::Master, true, 0);
  settings.setFlag(Settings::Messages, false, 0);
  settings.setFlag(Settings::Direct, true, 0);
  settings.setFlag(Settings::Mentions, true, 0);
  settings.notifyMessage(true, false, false, false);
  settings.notifyMessage(false, false, false, false);
  settings.notifyMessage(false, true, true, false);
  settings.notifyMessage(false, true, true, true);
  check(c.played == std::vector<unsigned>({1, 2}), "Notification routing/mute precedence changed");
  check(settings.preview(3) == Settings::Preview::Silent, "Invalid preview slot played");
  touchPrefsSetDndStartSlot(0);
  touchPrefsSetDndEndSlot(47);
  settings.stepTime(false, -1);
  settings.stepTime(true, 1);
  check(touchPrefsGetDndStartSlot() == 47 && touchPrefsGetDndEndSlot() == 0, "DND step failed to wrap");
  touchPrefsSetSoundVolume(95);
  settings.stepVolume(1);
  check(touchPrefsGetSoundVolume() == 100 && c.volume == 100, "Volume maximum/codec update failed");
  touchPrefsSetSoundVolume(5);
  settings.stepVolume(-1);
  check(touchPrefsGetSoundVolume() == 0 && c.volume == 0, "Volume minimum failed");
  touchPrefsSetSoundFile(1, "sd:/sounds/test.wav");
  char text[64];
  settings.fileName(1, text, sizeof text);
  check(!strcmp(text, "SD: test.wav"), "Sound filename lost SD origin");
  check(!settings.useBuiltin(3) && !settings.selectFileSlot(3), "Invalid sound slot accepted");
  check(settings.selectFileSlot(1) && settings.fileSlot() == 1 && settings.useBuiltin(1),
        "Sound slot not captured/reset");
  settings.fileName(1, text, sizeof text);
  check(!text[0], "Built-in slot retained path label");
  check(!settings.setColor(2, 0) && !settings.setColor(0, 7), "Invalid indicator choice accepted");
  settings.setColor(0, 3);
  settings.setColor(1, 4);
  settings.setFlag(Settings::Indicator, true, 0);
  settings.blink(false, 0xfffffff0u);
  settings.tick(0xffffffefu);
  check(c.outputs.empty(), "Indicator started before deadline");
  settings.tick(0xfffffff0u);
  settings.tick(10);
  check(c.outputs == std::vector<unsigned>({6}), "Indicator wrap scheduled duplicate transition");
  for (unsigned i = 1; i < 6; ++i)
    settings.tick(0xfffffff0u + i * 220u);
  settings.tick(10000);
  check(c.outputs == std::vector<unsigned>({6, 0, 6, 0, 6, 0}), "Indicator did not finish three blinks off");
  c.outputs.clear();
  c.ioFail = true;
  settings.blink(true, 100);
  settings.tick(100);
  settings.tick(149);
  check(c.outputs == std::vector<unsigned>({3}), "Indicator retry ran too soon");
  c.ioFail = false;
  settings.tick(150);
  settings.tick(370);
  check(c.outputs == std::vector<unsigned>({3, 3, 0}), "Failed indicator write consumed a transition");
  c.outputs.clear();
  c.ioFail = true;
  settings.setFlag(Settings::Indicator, false, 400);
  settings.tick(449);
  c.ioFail = false;
  settings.tick(450);
  settings.tick(1000);
  check(c.outputs == std::vector<unsigned>({0, 0}), "Disable did not retry final off write");
  c.outputs.clear();
  settings.blink(true, 1000);
  settings.tick(1000);
  check(c.outputs.empty(), "Disabled indicator blinked");
  settings.setFlag(Settings::Indicator, true, 500);
  settings.blink(true, 500);
  c.stopOnWrite = true;
  settings.tick(500);
  settings.tick(500);
  settings.tick(1000);
  check(c.outputs == std::vector<unsigned>({3, 0}), "Reentrant disable resurrected indicator sequence");
  Settings unavailable(host(c, {false, false, false, false, false}));
  const auto changes = c.changed;
  check(!unavailable.setFlag(Settings::Master, true, 0) && !unavailable.setColor(0, 1),
        "Unsupported controls changed preferences");
  unavailable.stepTime(false, 1);
  unavailable.stepVolume(1);
  check(changes == c.changed && unavailable.preview(0) == Settings::Preview::Silent &&
            !unavailable.useBuiltin(0),
        "Unsupported audio reached backend");
}
} // namespace
void runSoundSettingsRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Context c;
  Settings settings(host(c));
  c.settings = &settings;
  const auto original = settings.read();
  char originalFiles[3][TOUCH_SOUND_PATH_MAXLEN]{};
  for (unsigned i = 0; i < 3; ++i)
    touchPrefsGetSoundFile(i, originalFiles[i], sizeof originalFiles[i]);
  serviceRegression();
  touchPrefsSetDndEnabled(false);
  touchPrefsSetDndStartSlot(0);
  touchPrefsSetDndEndSlot(12);
  touchPrefsSetSoundVolume(50);
  touchPrefsSetSoundFile(0, "/msg.wav");
  touchPrefsSetSoundFile(1, "/dm.wav");
  touchPrefsSetSoundFile(2, "/mention.wav");
  Screen::Host callbacks{&c,
                         [](void *p, const char *text, int) {
                           auto &c = *static_cast<Context *>(p);
                           ++c.alerts;
                           snprintf(c.alert, sizeof c.alert, "%s", text);
                           if (c.replaceOnAlert) {
                             c.replaceOnAlert = false;
                             c.screen->build(c.replacement, 202);
                           }
                         },
                         [](void *p) {
                           auto &c = *static_cast<Context *>(p);
                           ++c.files;
                           c.screen->detach();
                         },
                         [](lv_obj_t **root) {
                           lv_obj_del_async(*root);
                           *root = nullptr;
                           if (closing && closing->replaceOnClose) {
                             closing->replaceOnClose = false;
                             closing->screen->build(closing->replacement, 202);
                           }
                         },
                         []() -> lv_coord_t { return 24; },
                         nullptr};
  Screen screen(settings, callbacks);
  c.screen = &screen;
  closing = &c;
  auto *first = body();
  screen.build(first, 202);
  lv_obj_update_layout(first);
  for (uint32_t i = 1; i < lv_obj_get_child_cnt(first); ++i) {
    auto *previous = lv_obj_get_child(first, i - 1), *current = lv_obj_get_child(first, i);
    check(lv_obj_get_y(current) >= lv_obj_get_y(previous) + lv_obj_get_height(previous),
          "Sound form rows overlap");
  }
  auto *master = toggle(first, TR("Sound")), *direct = toggle(first, TR("DM sound"));
  change(master, false);
  change(direct, true);
  check(c.played.empty() && !strcmp(c.alert, TR("Sound is off")), "Muted sound page previewed");
  change(master, true);
  check(c.played == std::vector<unsigned>({0}), "Master enable confirmation missing");
  c.minute = 60;
  change(toggle(first, TR("Do Not Disturb")), true);
  change(direct, true);
  check(c.played.size() == 1 && !strcmp(c.alert, TR("Do Not Disturb is on")), "DND preview was not blocked");
  click(button(first, "-30m"));
  check(touchPrefsGetDndStartSlot() == 47 && find(first, "23:30"), "Sound time row did not update");
  click(button(first, LV_SYMBOL_PLUS));
  check(touchPrefsGetSoundVolume() == 60 && find(first, "60%") && c.played.size() == 1,
        "Muted volume step beeped/lost value");
  click(button(first, "msg.wav"));
  auto *oldBuiltin = button(lv_layer_top(), TR("Built-in (default)"));
  screen.closeMenu();
  click(button(first, "dm.wav"));
  click(oldBuiltin);
  char text[128];
  touchPrefsGetSoundFile(1, text, sizeof text);
  check(screen.menuOpen() && !strcmp(text, "/dm.wav"), "Stale sound menu changed replacement slot");
  pump(2);
  click(button(lv_layer_top(), TR("Built-in (default)")));
  touchPrefsGetSoundFile(1, text, sizeof text);
  check(!screen.menuOpen() && !text[0] && find(first, TR("Built-in")),
        "Built-in choice did not update its slot");
  pump(2);
  click(button(first, "mention.wav"));
  click(button(lv_layer_top(), TR(LV_SYMBOL_DIRECTORY "  Choose .wav from files")));
  check(c.files == 1 && settings.fileSlot() == 2 && !screen.menuOpen(), "File chooser lost target slot");
  const auto changed = c.changed;
  change(master, false);
  click(button(first, LV_SYMBOL_PLUS));
  check(c.changed == changed, "Retired sound form changed settings");
  lv_obj_del(first);
  pump(2);
  auto *second = body();
  screen.build(second, 202);
  const auto count = lv_obj_get_child_cnt(second);
  screen.build(second, 202);
  check(lv_obj_get_child_cnt(second) == count, "Sound rebuild accumulated rows");
  auto *third = c.replacement = body();
  c.replaceOnChanged = true;
  change(toggle(second, TR("Sound")), false);
  check(find(third, "msg.wav") && !lv_obj_has_state(toggle(third, TR("Sound")), LV_STATE_CHECKED),
        "Sound mutation callback overwrote replacement form");
  lv_obj_del(second);
  auto *fourth = c.replacement = body();
  c.replaceOnRead = true;
  screen.refresh();
  check(find(fourth, "msg.wav"), "Sound read callback lost replacement");
  lv_obj_del(third);
  auto *fifth = c.replacement = body();
  c.replaceOnAlert = true;
  const auto plays = c.played.size();
  change(toggle(fourth, TR("Sound")), true);
  check(c.played.size() == plays, "Retired sound action previewed after alert replaced page");
  lv_obj_del(fourth);
  click(button(fifth, "msg.wav"));
  auto *sixth = c.replacement = body();
  c.replaceOnClose = true;
  click(button(lv_layer_top(), TR("Built-in (default)")));
  touchPrefsGetSoundFile(0, text, sizeof text);
  check(!strcmp(text, "/msg.wav") && find(sixth, "msg.wav"), "Reentrant menu close applied stale choice");
  lv_obj_del(fifth);
  pump(2);
  click(button(sixth, "msg.wav"));
  auto *menu = lv_obj_get_parent(lv_obj_get_parent(find(lv_layer_top(), TR("Built-in (default)"))));
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        menu, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(menu);
  check(deleted == 3 && !screen.menuOpen(), "Sound menu DELETE skipped observers");
  click(button(sixth, "msg.wav"));
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        sixth, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(sixth);
  pump(2);
  check(deleted == 6 && !screen.menuOpen(), "Sound form DELETE skipped observers or retained menu");
  auto *retained = body();
  {
    Screen temporary(settings, callbacks);
    temporary.build(retained, 202);
    click(button(retained, "msg.wav"));
  }
  const auto afterDestruction = c.changed;
  change(toggle(retained, TR("Sound")), false);
  check(c.changed == afterDestruction, "Destroyed sound owner retained callbacks");
  lv_obj_del(retained);
  pump(2);
  Settings limited(host(c, {false, false, false, false, true}));
  Screen indicators(limited, callbacks);
  auto *last = body();
  indicators.build(last, 202);
  check(find(last, TR("Incoming message blink")) && !find(last, TR("Sound")) && !find(last, TR("Volume")),
        "Indicator-only device exposed audio controls");
  // Choose blue in the first color row without relying on translated names.
  auto *swatches = lv_obj_get_child(last, 2);
  click(lv_obj_get_child(swatches, 2));
  check(touchPrefsGetAttakyNotifyRoomColor() == 2, "Indicator swatch did not persist");
  lv_obj_del(last);
  closing = nullptr;
  touchPrefsSetLoudAlerts(original.flags[Settings::Loud]);
  touchPrefsSetSoundMessages(original.flags[Settings::Messages]);
  touchPrefsSetSoundDirect(original.flags[Settings::Direct]);
  touchPrefsSetSoundMentions(original.flags[Settings::Mentions]);
  touchPrefsSetDndEnabled(original.flags[Settings::Dnd]);
  touchPrefsSetDndStartSlot(original.start);
  touchPrefsSetDndEndSlot(original.end);
  touchPrefsSetSoundVolume(original.volume);
  touchPrefsSetAttakyNotifyEnabled(original.flags[Settings::Indicator]);
  touchPrefsSetAttakyNotifyRoomColor(original.colors[0]);
  touchPrefsSetAttakyNotifyDmColor(original.colors[1]);
  for (unsigned i = 0; i < 3; ++i)
    touchPrefsSetSoundFile(i, originalFiles[i]);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Sound settings leaked LVGL roots");
  puts("Sound settings: quiet windows, preview/notification policy, blink retries, capabilities, captured "
       "menus and lifetimes passed.");
}
