// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>
#include <cstdint>

namespace ui {
namespace focus {

// All pointers are borrowed for one UI tick. The selector keeps no state.
struct ContextSnapshot {
  lv_obj_t *screen = nullptr;
  lv_obj_t *topLayer = nullptr;
  lv_obj_t *wifiSheet = nullptr;
  lv_obj_t *settingsSheet = nullptr;
  int settingsCategory = -1;
  lv_obj_t *chatOverlay = nullptr;
  const void *chatIdentity = nullptr;
  lv_obj_t *composer = nullptr;
  lv_obj_t *tabview = nullptr;
  int activeTab = 0;
  lv_obj_flag_t skipFlag = static_cast<lv_obj_flag_t>(0);
};

enum class ContextMode { Top, Wifi, Settings, Chat, Main };

struct FocusSelection {
  lv_obj_t *root = nullptr;
  ContextMode mode = ContextMode::Main;
  bool onPage = true;
  bool topLayer = false;
  uint32_t signature = 0;
  const void *chatIdentity = nullptr;
  lv_obj_t *composer = nullptr;
};

// Last visible, non-skipped child owns input on the top layer. Null means the
// layer has no active popup; hidden keyboard overlays do not change context.
lv_obj_t *frontmostVisibleTopChild(lv_obj_t *top, lv_obj_flag_t skipFlag);
FocusSelection resolveFocusContext(const ContextSnapshot &snapshot);

} // namespace focus
} // namespace ui
