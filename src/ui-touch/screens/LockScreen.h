// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../widgets/ObjectRef.h"
#include <cstdint>

namespace ui { namespace screens {

// Owns only the LVGL lock overlay, its wallpaper descriptor, and the unlock
// countdown. Clock/lock policy, storage, decoding, and backlight stay in Host.
// All calls, including Host callbacks, run on the UI thread.
class LockScreen {
public:
  enum class Layout : uint8_t { Standard, Pager };
  struct Snapshot {
    char clockText[12] = "--:--";
    int minute = 0;
    bool currentQuality = false;
    int unread = 0;
    uint32_t nowMs = 0;
    uint32_t textColor = 0xFFFFFF;
    Layout layout = Layout::Standard;
    char unlockHint[72] = {};
    lv_obj_flag_t navSkipFlag = static_cast<lv_obj_flag_t>(0);
  };
  struct Wallpaper {
    const uint8_t *pixels = nullptr;
    uint16_t width = 0, height = 0;
    bool owned = false; // decoded buffer; borrowed embedded RGB565 stays with Host
    uint16_t fitHeight = 0; // zero: cover wallpaper; otherwise fit an emblem without cropping
    int16_t centerOffsetY = 0;
  };
  struct Host {
    void *context = nullptr;
    bool (*readSnapshot)(void *, Snapshot &) = nullptr;
    // Queried on show and at most once per second while visible. The ordinary
    // clock snapshot may remain cheap on every UI loop tick.
    int (*readUnread)(void *) = nullptr;
    bool (*loadWallpaper)(void *, Wallpaper &) = nullptr;
    void (*releaseWallpaper)(void *, const uint8_t *) = nullptr;
    void (*statusBarTransparent)(void *, bool) = nullptr;
  };

  explicit LockScreen(Host host);
  ~LockScreen();
  LockScreen(const LockScreen &) = delete;
  LockScreen &operator=(const LockScreen &) = delete;

  void show(); // idempotent; a visible overlay is only moved to the front
  void hide();
  void service(); // minute/clock-quality change + wrap-safe 1 Hz unread poll
  void showUnlockProgress(uint32_t remainingMs);
  void hideUnlockProgress();
  bool visible() const { return _root.get() != nullptr; }
  lv_obj_t *root() const { return _root.get(); }

private:
  static void rootDeleted(lv_event_t *);
  static void popupDeleted(lv_event_t *);
  bool read(Snapshot &);
  bool still(uint32_t generation) const;
  void applyClock(const Snapshot &);
  void applyUnread(const Snapshot &);
  Wallpaper retireWallpaper();
  void clearRoot(bool deleting);
  void clearPopup(bool deleting);

  Host _host;
  widgets::ObjectRef _root, _clock, _unread, _status, _hint;
  widgets::ObjectRef _popup, _count;
  lv_img_dsc_t _wallDescriptor{}; // stable cache key for the owner's lifetime
  Wallpaper _wallpaper{};
  uint32_t _generation = 0, _lastUnreadMs = 0;
  int _minute = -1, _unreadCount = -1;
  int8_t _quality = -1;
  Layout _layout = Layout::Standard;
  bool _destroying = false;
};

} } // namespace ui::screens
