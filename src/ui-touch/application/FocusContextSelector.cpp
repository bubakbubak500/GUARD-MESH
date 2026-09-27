// SPDX-License-Identifier: GPL-3.0-or-later
#include "FocusContextSelector.h"

namespace ui {
namespace focus {
namespace {

bool valid(lv_obj_t *object) { return object && lv_obj_is_valid(object); }

} // namespace

lv_obj_t *frontmostVisibleTopChild(lv_obj_t *top, lv_obj_flag_t skipFlag) {
  if (!valid(top)) return nullptr;
  const int32_t count = static_cast<int32_t>(lv_obj_get_child_cnt(top));
  for (int32_t i = count - 1; i >= 0; --i) {
    lv_obj_t *child = lv_obj_get_child(top, static_cast<uint32_t>(i));
    if (child && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) &&
        (!skipFlag || !lv_obj_has_flag(child, skipFlag)))
      return child;
  }
  return nullptr;
}

FocusSelection resolveFocusContext(const ContextSnapshot &snapshot) {
  FocusSelection result;
  result.root = valid(snapshot.screen) ? snapshot.screen : nullptr;
  if (lv_obj_t *front = frontmostVisibleTopChild(snapshot.topLayer, snapshot.skipFlag)) {
    result.root = front;
    result.mode = ContextMode::Top;
    result.onPage = false;
    result.topLayer = true;
    result.signature = 0xA5A5A5A5u;
    return result;
  }
  // Base-layer sheets have historically taken priority even if HIDDEN; changing
  // that rule would alter existing navigation and belongs in a separate fix.
  if (valid(snapshot.wifiSheet)) {
    result.root = snapshot.wifiSheet;
    result.mode = ContextMode::Wifi;
    result.onPage = false;
    result.signature = 0x9F1CB0EDu;
    return result;
  }
  if (valid(snapshot.settingsSheet)) {
    result.root = snapshot.settingsSheet;
    result.mode = ContextMode::Settings;
    result.onPage = false;
    result.signature = 0x5E771465u ^
        (static_cast<uint32_t>(snapshot.settingsCategory + 1) * 2654435761u);
    return result;
  }
  if (valid(snapshot.chatOverlay)) {
    result.root = snapshot.chatOverlay;
    result.mode = ContextMode::Chat;
    result.onPage = false;
    result.signature = 0xC4A7C4A7u;
    result.chatIdentity = snapshot.chatIdentity;
    result.composer = snapshot.composer;
    return result;
  }

  result.signature = (static_cast<uint32_t>(snapshot.activeTab) + 1u) * 2654435761u;
  if (valid(snapshot.tabview)) {
    lv_obj_t *content = lv_tabview_get_content(snapshot.tabview);
    const uint32_t index = static_cast<uint32_t>(snapshot.activeTab);
    if (valid(content) && index < lv_obj_get_child_cnt(content))
      result.root = lv_obj_get_child(content, index);
  }
  return result;
}

} // namespace focus
} // namespace ui
