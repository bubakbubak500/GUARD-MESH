// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include <lvgl.h>
namespace ui {
namespace screens {
namespace threadMenu {
struct Thread {
  int index = -1, channelSlot = -1;
  char name[MessageTypes::MAX_THREAD_NAME + 1] = {};
  bool channel = false, room = false, hasContact = false, hasSecret = false;
  uint8_t contact[32] = {}, secret[16] = {};
};
using IconResult = void (*)(uint32_t, const char *);
struct Host {
  bool (*read)(int, Thread &);
  int (*statusHeight)();
  void (*closeRoot)(lv_obj_t **);
  void (*prepareConfirm)();
  void (*focusConfirm)(lv_obj_t *);
  void (*sanitize)(const lv_font_t *, char *, size_t, const char *);
  void (*alert)(const char *, unsigned);
  void (*markRead)(int);
  void (*erase)(const Thread &);
  void (*clearHistory)(int);
  void (*resetPath)(const uint8_t *);
  void (*login)(const uint8_t *);
  void (*join)(const uint8_t *);
  void (*scope)(int, const char *);
  void (*blocked)();
  uint8_t (*mute)(const char *);
  void (*setMute)(const char *, uint8_t);
  void (*setIcon)(const char *, const char *);
  void (*pickIcon)(IconResult, uint32_t);
  void (*cancelIcon)(IconResult);
  lv_obj_t *(*shareBody)();
  bool (*shareActive)(lv_obj_t *);
  void (*closeShare)(lv_obj_t *);
  void (*copySecret)(const char *);
};
// One UI-thread owner for menu, confirmation, channel-share bytes and pending icon request.
// read() returns a value snapshot; commands run only after revalidating its identity.
void configure(const Host &);
void show(int index);
void close();
bool isOpen();
} // namespace threadMenu
} // namespace screens
} // namespace ui
