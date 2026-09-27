// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include <lvgl.h>
namespace ui {
namespace screens {
namespace messageMenu {
using Message = MessageTypes::UIMessage;
struct Host {
  bool (*readMessage)(int, Message &);
  bool (*activeConversation)(const Message &);
  int (*statusHeight)();
  void (*closeRoot)(lv_obj_t **);
  void (*closeInfo)();
  void (*copy)(const char *);
  void (*info)(int);
  void (*insert)(bool channel, const char *);
  void (*block)(const char *sender);
  void (*resend)(const char *text);
  void (*erase)(int);
  void (*prepareRetry)();
  void (*focusConfirmation)(lv_obj_t *);
};
// UI-thread singleton. Owns its message snapshot and LVGL menu. Commands are
// dispatched only while the captured message and conversation remain current.
void configure(const Host &);
void show(int ringIndex);
void retry(int ringIndex);
void close();
bool isOpen();
} // namespace messageMenu
} // namespace screens
} // namespace ui
