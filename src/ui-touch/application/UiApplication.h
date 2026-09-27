// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace ui {
// Navigation policy and cross-task invalidations. Only post() is callable from
// workers; page state and popup callbacks belong exclusively to the UI thread.
class UiApplication {
public:
  using Close = void (*)();
  enum PopupFlags : uint8_t { Count = 1, BlockSwipe = 2, BasePage = 4, Drawer = 8, StatusPage = 16 };
  struct Popup { bool (*is_open)(); Close close; uint8_t flags; };
  enum class Dismiss { None = 0, Closed = 1, Blocked = -1 };
  enum Event : uint32_t { ContactsChanged = 1, WebMessagesChanged = 2 };

  void configurePopups(const Popup* entries, size_t count);
  // StatusPage entries remain dismissable/popups but do not shield their own
  // shared Back control. Callers can ignore that flag when computing the scrim.
  bool anyPopup(bool above_base = false, uint8_t ignore_flags = 0) const;
  bool blocksSwipe() const;
  Dismiss dismissTop(bool above_drawer = false);

  void beginPage(const char* title, Close close, bool slim = false);
  bool endPage(Close owner);
  bool collapsePage(Close owner);
  const char* pageTitle() const { return _page_open ? _page_title : nullptr; }
  Close pageClose() const { return _page_close; }
  bool pageSlim() const { return _page_slim; }

  void post(Event event) { _events.fetch_or(event, std::memory_order_release); }
  bool pending(Event event) const { return (_events.load(std::memory_order_acquire) & event) != 0; }
  bool consume(Event event) { return (_events.fetch_and(~uint32_t(event), std::memory_order_acq_rel) & event) != 0; }
private:
  const Popup* _popups = nullptr;
  size_t _popup_count = 0;
  // Copy translated/dynamic titles so a caller's temporary buffer cannot dangle.
  char _page_title[128] = {};
  Close _page_close = nullptr;
  bool _page_open = false, _page_slim = false;
  std::atomic<uint32_t> _events{0};
};
}
