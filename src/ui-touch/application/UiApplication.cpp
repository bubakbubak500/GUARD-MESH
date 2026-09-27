// SPDX-License-Identifier: GPL-3.0-or-later
#include "UiApplication.h"
#include <cstring>

namespace ui {
void UiApplication::configurePopups(const Popup* entries, size_t count) {
  _popups = entries;
  _popup_count = entries ? count : 0;
}
bool UiApplication::anyPopup(bool above_base, uint8_t ignore_flags) const {
  for (size_t i = 0; i < _popup_count; ++i) {
    const auto& popup = _popups[i];
    if ((popup.flags & Count) && !(popup.flags & ignore_flags) &&
        (!above_base || !(popup.flags & BasePage)) && popup.is_open()) return true;
  }
  return false;
}
bool UiApplication::blocksSwipe() const {
  for (size_t i = 0; i < _popup_count; ++i)
    if ((_popups[i].flags & BlockSwipe) && _popups[i].is_open()) return true;
  return false;
}
UiApplication::Dismiss UiApplication::dismissTop(bool above_drawer) {
  for (size_t i = 0; i < _popup_count; ++i) {
    const auto& popup = _popups[i];
    if (above_drawer && (popup.flags & Drawer)) continue;
    if (!popup.is_open()) continue;
    // A progress overlay blocks navigation to every page underneath it.
    if (!popup.close) return Dismiss::Blocked;
    popup.close();
    return Dismiss::Closed;
  }
  return Dismiss::None;
}
void UiApplication::beginPage(const char* title, Close close, bool slim) {
  _page_open = title != nullptr;
  std::strncpy(_page_title, title ? title : "", sizeof(_page_title) - 1);
  _page_title[sizeof(_page_title) - 1] = '\0';
  _page_close = close;
  _page_slim = slim;
}
bool UiApplication::endPage(Close owner) {
  if (_page_close != owner) return false;
  _page_open = false;
  _page_title[0] = '\0';
  _page_close = nullptr;
  _page_slim = false;
  return true;
}
bool UiApplication::collapsePage(Close owner) {
  if (_page_close != owner) return false;
  _page_title[0] = '\0';
  _page_slim = true;
  return true;
}
}
