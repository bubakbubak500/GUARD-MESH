// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/MessageTypes.h"
#include "ChatComposer.h"
#include <lvgl.h>
namespace ui {
namespace screens {
// ---- Per-panel state (DM chats and channels share this shape) ----
struct ChatPanel {
  lv_obj_t *list_cont;       // full-page lv_list in the tab (thread chooser)
  lv_obj_t *overlay;         // full-screen overlay on lv_scr_act (chat detail)
  lv_obj_t *header_name;     // label: thread name in the overlay header
  lv_obj_t *msgs;            // read-only textarea: message history
  lv_obj_t *jump_btn;        // floating "jump to latest" button (Discord-style)
  lv_obj_t *jump_oldest_btn; // floating "jump to oldest" button
  ChatComposer composer;
  bool channel_mode;
  /** When true, `list_cont` shows channels + DMs with history (Chats tab only). */
  bool inbox_combined;
  bool detail_open;
};

} // namespace screens
} // namespace ui
