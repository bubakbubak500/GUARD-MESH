// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include <lvgl.h>
namespace ui {
namespace screens {
namespace messageInfo {
using Message = MessageTypes::UIMessage;
struct Host {
  bool (*readMessage)(int, Message &);
  bool (*activeConversation)(const Message &);
  int (*statusHeight)();
  void (*closeRoot)(lv_obj_t **);
  int (*prepareRoute)(const Message &);
  void (*trace)();
  void (*replay)();
  const char *(*regionName)(uint8_t);
  uint8_t (*repeats)(uint32_t);
  uint8_t (*repeatCount)(uint32_t);
  uint8_t (*repeatHop)(uint32_t, uint8_t, uint8_t *, uint8_t);
  bool (*hopName)(const uint8_t *, int, char *, size_t);
  void (*sanitize)(const lv_font_t *, char *, size_t, const char *);
  lv_event_cb_t copyLabel, clampScroll;
};
// UI-thread owner for message metadata and trace-result dialogs.
void configure(const Host &);
void show(int ringIndex);
void close();
bool isOpen();
lv_obj_t *scrollBody(); // borrowed until close or external DELETE
void showTraceResult(const char *title, const char *body);
void closeTraceResult();
bool traceResultOpen();
} // namespace messageInfo
} // namespace screens
} // namespace ui
