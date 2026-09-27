// SPDX-License-Identifier: GPL-3.0-or-later
#include "UITask.h"
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool okay, const char *message) {
  if (!okay) throw std::runtime_error(message);
}
}

// Exercise real UITask adapters and loop, in addition to deterministic policy
// tests. Hardware paint/rail behavior still needs the corresponding device.
void runScreenPolicyIntegration(UITask &task, void (*pump)(unsigned)) {
  const uint16_t timeout = task.getScreenTimeoutSecs();
  check(task.setScreenTimeoutSecs(0), "Screen policy: timeout setup failed");
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
