// SPDX-License-Identifier: GPL-3.0-or-later
#include "UITask.h"
#include "SimPlatform.h"
#include "i18n.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

void guardSimCaptureDisplayFrame(lv_color_t* pixels);
void guardSimDisplayOpenHome();

namespace {
void check(bool okay, const char *message) {
  if (!okay) throw std::runtime_error(message);
}

void countDraw(lv_event_t* event) {
  ++*static_cast<unsigned*>(lv_event_get_user_data(event));
}
lv_obj_t* colorPatch(lv_obj_t* parent, int x, int y, unsigned* draws) {
  auto* patch = lv_obj_create(parent);
  lv_obj_remove_style_all(patch);
  lv_obj_set_pos(patch, x, y);
  lv_obj_set_size(patch, 12, 12);
  lv_obj_set_style_bg_opa(patch, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(patch, lv_color_hex(0x0000FF), 0);
  lv_obj_add_event_cb(patch, countDraw, LV_EVENT_DRAW_MAIN, draws);
  return patch;
}

void runDisplayPowerIntegration(UITask& task, void (*pump)(unsigned)) {
  const bool messageFlash = touchPrefsGetMsgFlash();
  touchPrefsSetMsgFlash(false);
  guardSimDisplayOpenHome();
  task.unlockScreen();
  pump(400);
  unsigned draws = 0, ticks = 0;
  auto* timer = lv_timer_create([](lv_timer_t* t) {
    ++*static_cast<unsigned*>(t->user_data);
  }, 10, &ticks);
  auto* screenPatch = colorPatch(lv_scr_act(), 30, 65, &draws);
  auto* topPatch = colorPatch(lv_layer_top(), 60, 65, &draws);
  auto* sysPatch = colorPatch(lv_layer_sys(), 90, 65, &draws);
  lv_refr_now(nullptr);
  check(display.pixels[70 * 320 + 35] == lv_color_hex(0x0000FF).full,
        "Display power: initial physical screen patch missing");
  task.sleepScreen();
  check(!display.backlightOn, "Display power: sleep did not darken backlight");
  const unsigned darkWrites = display.writes;
  const unsigned darkDraws = draws;
  for (auto* patch : {screenPatch, topPatch, sysPatch})
    lv_obj_set_style_bg_color(patch, lv_color_hex(0xFF0000), 0);
  pump(400);
  check(ticks > 10, "Display power: dark screen stopped LVGL application timers");
  check(display.writes == darkWrites && draws == darkDraws,
        "Display power: dark invalidation or periodic update rendered/transferred pixels");

  // Explicit redraw remains legal but may not leak a transfer to dark GRAM.
  lv_refr_now(nullptr);
  check(draws > darkDraws && display.writes == darkWrites,
        "Display power: forced dark redraw stalled or wrote the physical panel");
  task.noteUserInput(); // first touch wakes, preserving the existing input rule
  check(!task.isScreenOff() && display.backlightOn && display.writesAtLastLight > darkWrites,
        "Display power: first input did not repaint before lighting");
  for (int x : {35, 65, 95})
    check(display.pixelsAtLastLight[70 * 320 + x] == lv_color_hex(0xFF0000).full,
          "Display power: wake lit stale active/top/system layer pixels");

  // A wake originating in an LVGL callback must defer heavy repaint until the
  // handler returns. Its remaining widget mutations belong in the first frame.
  task.sleepScreen();
  struct CallbackWake {
    UITask* task;
    lv_obj_t* patch;
    bool ran = false, stayedDark = false;
  } callbackWake{&task, sysPatch};
  auto* wakeTimer = lv_timer_create([](lv_timer_t* t) {
    auto& context = *static_cast<CallbackWake*>(t->user_data);
    context.task->noteUserInput();
    context.ran = true;
    context.stayedDark = !display.backlightOn;
    lv_obj_set_style_bg_color(context.patch, lv_color_hex(0x0000FF), 0);
  }, 1, &callbackWake);
  lv_timer_set_repeat_count(wakeTimer, 1);
  pump(50);
  check(callbackWake.ran && callbackWake.stayedDark && display.backlightOn &&
        display.pixelsAtLastLight[70 * 320 + 95] == lv_color_hex(0x0000FF).full,
        "Display power: callback wake repainted recursively or lit before its mutations committed");

  task.lockScreen();
  lv_obj_move_foreground(topPatch);
  lv_obj_move_foreground(sysPatch);
  lv_obj_set_style_bg_color(topPatch, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_bg_color(sysPatch, lv_color_hex(0x00FF00), 0);
  const unsigned lockedWrites = display.writes;
  const int messageCount = task.getMsgCount();
  UIMessageEvent incoming(UIEventType::channelMessage, 0, nullptr, "#guardian-sim",
                          "Received while display rendering is parked", messageCount + 1);
  task.receiveMessage(incoming);
  pump(60);
  bool received = false;
  for (int i = 0; i < task.msgCap(); ++i) {
    UITask::UIMessage message{};
    if (task.getMessageByIndex(i, message) && !message.outgoing &&
        !strcmp(message.text, "Received while display rendering is parked")) received = true;
  }
  check(task.isScreenOff() && task.isManualLocked() && received,
        "Display power: dark lock lost a message or unlocked on receive");
  check(display.writes == lockedWrites, "Display power: locked receive wrote to the panel");

  std::vector<lv_color_t> capture(320 * 240);
  guardSimCaptureDisplayFrame(capture.data());
  check(capture[70 * 320 + 65].full == lv_color_hex(0x00FF00).full &&
        capture[70 * 320 + 95].full == lv_color_hex(0x00FF00).full &&
        display.writes == lockedWrites && task.isScreenOff(),
        "Display power: dark screenshot omitted overlays, transferred pixels or woke the panel");
  const unsigned capturedDraws = draws;
  const unsigned webFrames = g_web_mirror.frames;
  g_web_mirror.connected = true;
  lv_obj_invalidate(topPatch);
  pump(80);
  check(draws > capturedDraws && g_web_mirror.frames > webFrames &&
        display.writes == lockedWrites && task.isScreenOff(),
        "Display power: dark web mirror failed to render/send or wrote/woke the panel");
  g_web_mirror.connected = false;

  task.lockscreenReveal();
  check(!task.isScreenOff() && task.isManualLocked() &&
        display.writesAtLastLight > lockedWrites &&
        display.pixelsAtLastLight[70 * 320 + 95] == lv_color_hex(0x00FF00).full,
        "Display power: lock reveal lit stale pixels or cleared the lock");
  task.sleepScreen();
  const unsigned unlockWrites = display.writes;
  task.unlockScreen();
  check(!task.isScreenOff() && !task.isManualLocked() && display.writesAtLastLight > unlockWrites &&
        display.pixelsAtLastLight[70 * 320 + 35] == lv_color_hex(0xFF0000).full,
        "Display power: unlock lit the lock overlay or omitted the underlying frame");
  lv_obj_del(screenPatch);
  lv_obj_del(topPatch);
  lv_obj_del(sysPatch);
  lv_timer_del(timer);
  touchPrefsSetMsgFlash(messageFlash);
  pump(30);
  puts("Display power: dark render/SPI suppression, live timers/message ingest, first wake, all layers, "
       "screenshot, web mirror, lock reveal and unlock: PASS");
}
}

// Exercise real UITask adapters and loop, in addition to deterministic policy
// tests. Hardware paint/rail behavior still needs the corresponding device.
void runScreenPolicyIntegration(UITask &task, void (*pump)(unsigned)) {
  const uint16_t timeout = task.getScreenTimeoutSecs();
  check(task.setScreenTimeoutSecs(0), "Screen policy: timeout setup failed");
  runDisplayPowerIntegration(task, pump);
  task.unlockScreen();
  task.sleepScreen();
  check(task.isScreenOff() && !task.isManualLocked(), "Screen policy: soft sleep locked input");
  task.noteUserInput();
  check(!task.isScreenOff() && !task.isManualLocked(), "Screen policy: idle touch did not wake");

  task.lockScreen();
  check(task.isScreenOff() && task.isManualLocked(), "Screen policy: T-Deck lock did not dim");
  task.noteUserInput();
  check(task.isScreenOff() && task.isManualLocked(), "Screen policy: touch bypassed hard lock");
  task.lockscreenReveal();
  check(!task.isScreenOff() && task.isManualLocked(), "Screen policy: reveal unlocked device");
  pump(30);
  task.sleepScreen();
  check(task.isScreenOff() && task.isManualLocked(), "Screen policy: dim lost hard lock");
  task.lockscreenReveal();
  task.unlockScreen();
  check(!task.isScreenOff() && !task.isManualLocked(), "Screen policy: deliberate unlock failed");

  check(task.setScreenTimeoutSecs(1), "Screen policy: idle setup failed");
  pump(1150);
  check(task.isScreenOff(), "Screen policy: graphical loop did not apply idle deadline");
  task.unlockScreen();
  check(task.setScreenTimeoutSecs(timeout), "Screen policy: timeout restore failed");
  puts("Screen policy UITask integration: PASS");
}
