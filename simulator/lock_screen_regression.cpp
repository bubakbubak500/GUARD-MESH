// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/LockScreen.h"
#include "i18n.h"
#include "theme/Theme.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Screen = ui::screens::LockScreen;
const char *scenario = "setup";

void check(bool okay, const char *message) {
  if (okay) return;
  char detail[320];
  std::snprintf(detail, sizeof detail, "Lock screen '%s': %s", scenario, message);
  throw std::runtime_error(detail);
}

lv_obj_t *findLabel(lv_obj_t *root, const char *text) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class) &&
      std::strcmp(lv_label_get_text(root), text) == 0) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findLabel(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
lv_obj_t *firstImage(lv_obj_t *root) {
  if (!root) return nullptr;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *child = lv_obj_get_child(root, i);
    if (lv_obj_check_type(child, &lv_img_class)) return child;
  }
  return nullptr;
}
lv_obj_t *topPopup(Screen &screen) {
  auto *top = lv_layer_top();
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(top); ++i) {
    auto *child = lv_obj_get_child(top, i);
    if (child != screen.root() && findLabel(child, TR("keep holding"))) return child;
  }
  return nullptr;
}
void observedDelete(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}

struct Fixture {
  enum class Wall { None, Borrowed, Owned, Invalid };
  static uint8_t borrowed[16 * 16 * sizeof(lv_color_t)];
  Screen *screen = nullptr;
  Screen::Snapshot state{};
  Wall wall = Wall::Borrowed;
  unsigned reads = 0, unreadReads = 0, loads = 0, releases = 0, statusOn = 0, statusOff = 0;
  unsigned invalidatedBeforeRelease = 0;
  bool reenterOnRelease = false, reenterOnLoad = false, reenterOnStatus = false;
  bool deleteOldOnRelease = false;
  bool useUnreadCallback = false;
  int unreadValue = 0;
  lv_obj_t *replacement = nullptr;
  lv_obj_t *oldRoot = nullptr, *oldPopup = nullptr;
  _lv_img_cache_entry_t *cache = nullptr;
  const void *descriptor = nullptr;
  const uint8_t *latestPixels = nullptr;
  std::vector<bool> statusCalls;

  Fixture() {
    std::snprintf(state.clockText, sizeof state.clockText, "08:41");
    std::snprintf(state.unlockHint, sizeof state.unlockHint, "hold to unlock");
    state.minute = 41;
    state.currentQuality = true;
    state.textColor = 0xAABBCC;
    state.nowMs = 10;
  }
  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  static bool read(void *context, Screen::Snapshot &out) {
    auto &f = self(context);
    ++f.reads;
    out = f.state;
    return true;
  }
  static int unread(void *context) {
    auto &f = self(context);
    ++f.unreadReads;
    return f.unreadValue;
  }
  static bool load(void *context, Screen::Wallpaper &out) {
    auto &f = self(context);
    ++f.loads;
    if (f.wall == Wall::None) return false;
    out.width = f.wall == Wall::Invalid ? 0 : 16;
    out.height = 16;
    out.owned = f.wall != Wall::Borrowed;
    if (out.owned) {
      auto *pixels = new uint8_t[16 * 16 * sizeof(lv_color_t)];
      std::memset(pixels, 0x35 + (f.loads & 15), 16 * 16 * sizeof(lv_color_t));
      out.pixels = pixels;
    } else out.pixels = borrowed;
    f.latestPixels = out.pixels;
    if (f.reenterOnLoad) {
      f.reenterOnLoad = false;
      f.screen->hide();
      f.screen->show();
      f.screen->showUnlockProgress(2500);
      f.replacement = f.screen->root();
    }
    return true;
  }
  static void release(void *context, const uint8_t *pixels) {
    auto &f = self(context);
    ++f.releases;
    if (f.cache && f.cache->dec_dsc.src == nullptr)
      ++f.invalidatedBeforeRelease;
    delete[] pixels;
    if (f.deleteOldOnRelease) {
      f.deleteOldOnRelease = false;
      // A storage callback is permitted to retire either LVGL tree before
      // opening a replacement. clearRoot must not reuse either raw pointer.
      if (f.oldPopup) lv_obj_del(f.oldPopup);
      if (f.oldRoot) lv_obj_del(f.oldRoot);
      f.screen->show();
      f.screen->showUnlockProgress(2500);
      f.replacement = f.screen->root();
    }
    if (f.reenterOnRelease) {
      f.reenterOnRelease = false;
      f.screen->show();
      f.screen->showUnlockProgress(2500);
      f.replacement = f.screen->root();
    }
  }
  static void status(void *context, bool transparent) {
    auto &f = self(context);
    f.statusCalls.push_back(transparent);
    if (transparent) ++f.statusOn;
    else ++f.statusOff;
    if (transparent && f.reenterOnStatus) {
      f.reenterOnStatus = false;
      f.screen->hide();
      f.screen->show();
      f.screen->showUnlockProgress(2500);
      f.replacement = f.screen->root();
    }
  }
  Screen::Host host() {
    Screen::Host result;
    result.context = this;
    result.readSnapshot = read;
    if (useUnreadCallback) result.readUnread = unread;
    result.loadWallpaper = load;
    result.releaseWallpaper = release;
    result.statusBarTransparent = status;
    return result;
  }
  void openCache() {
    auto *image = firstImage(screen->root());
    check(image != nullptr, "loaded wallpaper image missing");
    descriptor = lv_img_get_src(image);
    auto *imageDescriptor = static_cast<const lv_img_dsc_t *>(descriptor);
    check(imageDescriptor && imageDescriptor->data == latestPixels &&
          imageDescriptor->header.w == 16 && imageDescriptor->header.h == 16,
          "wallpaper descriptor does not own the loaded pixels and dimensions");
    cache = _lv_img_cache_open(descriptor, lv_color_white(), 0);
    check(cache && cache->dec_dsc.src == descriptor, "wallpaper did not enter the image cache");
  }
};
uint8_t Fixture::borrowed[16 * 16 * sizeof(lv_color_t)] = {};

void expectNoTree(unsigned baseline) {
  check(lv_obj_get_child_cnt(lv_layer_top()) == baseline, "lock overlay or countdown leaked");
}
} // namespace

void runLockScreenRegression(void (*pump)(unsigned)) {
  const unsigned baseline = lv_obj_get_child_cnt(lv_layer_top());

  scenario = "borrowed wallpaper and idempotent show";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    check(screen.visible() && screen.root() && f.loads == 1 && f.statusOn == 1,
          "first show did not create one visible overlay");
    auto *first = screen.root();
    f.openCache();
    const void *descriptor = f.descriptor;
    screen.show();
    check(screen.root() == first && f.loads == 1 && f.releases == 0,
          "second show decoded or replaced a visible wallpaper");
    screen.hide();
    check(!screen.visible() && f.releases == 0 && f.statusOff == 1,
          "borrowed embedded wallpaper was released or status bar stayed transparent");
    check(f.cache->dec_dsc.src == nullptr, "borrowed wallpaper cache survived hide");
    screen.show();
    check(screen.visible() && f.loads == 2 && lv_img_get_src(firstImage(screen.root())) == descriptor,
          "new show did not reuse the stable descriptor with fresh source data");
    screen.hide();
    pump(2);
    expectNoTree(baseline);
  }

  scenario = "owned wallpaper, failed decode and destructor";
  {
    Fixture f; f.wall = Fixture::Wall::Owned;
    {
      Screen screen(f.host()); f.screen = &screen;
      screen.show(); f.openCache();
      screen.showUnlockProgress(2500);
      check(topPopup(screen) && findLabel(topPopup(screen), "3"),
            "unlock countdown did not ceil milliseconds to seconds");
      screen.showUnlockProgress(1000);
      check(findLabel(topPopup(screen), "1"), "unlock countdown did not update");
      screen.hideUnlockProgress();
      check(!topPopup(screen) && screen.visible(), "closing countdown also closed lock overlay");
      screen.showUnlockProgress(500);
    }
    check(f.releases == 1 && f.invalidatedBeforeRelease == 1 && f.statusOff == 1,
          "destructor failed to invalidate and release one owned wallpaper");
    pump(2); expectNoTree(baseline);
    f.wall = Fixture::Wall::Invalid;
    Screen failed(f.host()); f.screen = &failed;
    failed.show();
    check(f.loads == 2 && f.releases == 2 && !firstImage(failed.root()),
          "invalid decoded wallpaper retained its owned buffer or image widget");
    failed.hide();
    pump(2); expectNoTree(baseline);
  }

  scenario = "clock quality, minute, unread cadence and wrap";
  {
    Fixture f; f.wall = Fixture::Wall::None;
    f.useUnreadCallback = true;
    f.state.unread = 99; // callback is authoritative when configured
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    check(f.unreadReads == 1 && !findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  99"),
          "show did not use one bounded unread callback");
    auto *clock = findLabel(screen.root(), "08:41");
    auto *standardHint = findLabel(screen.root(), "hold to unlock");
    auto *standardStatus = findLabel(screen.root(), TR("Screen locked"));
    check(clock && standardHint && standardStatus &&
          lv_obj_get_style_align(clock, LV_PART_MAIN) == LV_ALIGN_TOP_MID &&
          lv_obj_get_style_align(standardStatus, LV_PART_MAIN) == LV_ALIGN_TOP_MID &&
          lv_obj_get_style_align(standardHint, LV_PART_MAIN) == LV_ALIGN_BOTTOM_MID,
          "standard lock labels lost centered alignment");
    check(clock != nullptr && lv_color_to32(lv_obj_get_style_text_color(clock, LV_PART_MAIN)) ==
          lv_color_to32(lv_color_hex(f.state.textColor)), "initial clock color or text wrong");
    f.state.currentQuality = false;
    screen.service();
    check(lv_color_to32(lv_obj_get_style_text_color(clock, LV_PART_MAIN)) ==
          lv_color_to32(lv_color_hex(ui::theme::colors().COLOR_STATUS_WARN)),
          "quality loss within one minute did not change warning color");
    f.state.currentQuality = true;
    f.state.minute = 42;
    std::snprintf(f.state.clockText, sizeof f.state.clockText, "08:42");
    screen.service();
    check(findLabel(screen.root(), "08:42") == clock,
          "minute rollover failed to update the existing clock widget");
    f.unreadValue = 3; f.state.nowMs = 999;
    screen.service();
    check(f.unreadReads == 1 && !findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  3"),
          "unread count updated before the 1 Hz boundary");
    f.state.nowMs = 1010;
    screen.service();
    check(f.unreadReads == 2 && findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  3"),
          "unread count did not update at the 1 Hz boundary");
    f.unreadValue = 4; f.state.nowMs = 1500;
    screen.service();
    check(f.unreadReads == 2 && findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  3"),
          "unread count changed before the next poll");
    f.state.nowMs = 2010;
    screen.service();
    check(f.unreadReads == 3 && findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  4"),
          "unread count did not update on the next poll");
    screen.hide();
    f.state.nowMs = UINT32_MAX - 500u; f.unreadValue = 1;
    screen.show();
    check(f.unreadReads == 4, "reopened lock did not read unread count exactly once");
    f.state.nowMs = 500; f.unreadValue = 2;
    screen.service();
    check(f.unreadReads == 5 && findLabel(screen.root(), LV_SYMBOL_ENVELOPE "  2"),
          "1 Hz unread poll failed across uint32 tick wrap");
    screen.hide();
    pump(2); expectNoTree(baseline);
  }

  scenario = "Pager alignment and external DELETE observers";
  {
    Fixture f; f.wall = Fixture::Wall::None;
    f.state.layout = Screen::Layout::Pager;
    f.state.navSkipFlag = LV_OBJ_FLAG_USER_1;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    auto *root = screen.root();
    auto *clock = findLabel(root, "08:41");
    auto *hint = findLabel(root, "hold to unlock");
    auto *locked = findLabel(root, TR("Screen locked"));
    check(clock && hint && locked && lv_obj_has_flag(root, LV_OBJ_FLAG_USER_1),
          "Pager root omitted navigation exclusion or labels");
    check(lv_obj_get_width(clock) == 192 &&
          lv_obj_get_style_align(clock, LV_PART_MAIN) == LV_ALIGN_TOP_LEFT &&
          lv_obj_get_style_align(hint, LV_PART_MAIN) == LV_ALIGN_BOTTOM_RIGHT,
          "Pager clock or hint lost board-specific alignment");
    screen.showUnlockProgress(2500);
    unsigned deletes = 0;
    lv_obj_add_event_cb(root, observedDelete, LV_EVENT_DELETE, &deletes);
    lv_obj_add_event_cb(root, observedDelete, LV_EVENT_DELETE, &deletes);
    lv_obj_del(root);
    check(deletes == 2 && !screen.visible() && !topPopup(screen) && f.statusOff == 1,
          "external root DELETE skipped observers or leaked countdown/status");
    screen.service();
    screen.hide();
    pump(2); expectNoTree(baseline);
  }

  scenario = "release callback replaces lock and popup";
  {
    Fixture f; f.wall = Fixture::Wall::Owned;
    Screen screen(f.host()); f.screen = &screen;
    screen.show(); f.openCache();
    f.reenterOnRelease = true;
    auto *old = screen.root();
    screen.hide();
    check(screen.visible() && screen.root() != old && screen.root() == f.replacement &&
          topPopup(screen) && findLabel(topPopup(screen), "3"),
          "retired root removed a replacement overlay or countdown from release callback");
    check(f.releases == 1 && f.invalidatedBeforeRelease == 1 &&
          f.statusCalls.back(), "retired root restored status bar over replacement");
    screen.hide();
    check(f.releases == 2, "replacement owned wallpaper was not released exactly once");
    pump(2); expectNoTree(baseline);
  }

  scenario = "load and status callbacks replace in-progress show";
  {
    Fixture f; f.wall = Fixture::Wall::Owned;
    Screen screen(f.host()); f.screen = &screen;
    f.reenterOnLoad = true;
    screen.show();
    check(screen.visible() && screen.root() == f.replacement && topPopup(screen) &&
          f.loads == 2 && f.releases == 1,
          "stale load continuation damaged replacement or leaked incoming buffer");
    screen.hide();
    check(f.releases == 2, "replacement after load callback leaked its wallpaper");
    pump(2); expectNoTree(baseline);

    f.reenterOnStatus = true;
    screen.show();
    check(screen.visible() && screen.root() == f.replacement && topPopup(screen) &&
          f.statusCalls.back(), "stale status-bar continuation replaced the new overlay");
    screen.hide();
    pump(2); expectNoTree(baseline);
  }

  scenario = "release callback deletes retired LVGL trees";
  {
    Fixture f; f.wall = Fixture::Wall::Owned;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    screen.showUnlockProgress(2500);
    f.oldRoot = screen.root();
    f.oldPopup = topPopup(screen);
    check(f.oldPopup != nullptr, "precondition countdown missing");
    unsigned rootDeletes = 0, popupDeletes = 0;
    lv_obj_add_event_cb(f.oldRoot, observedDelete, LV_EVENT_DELETE, &rootDeletes);
    lv_obj_add_event_cb(f.oldRoot, observedDelete, LV_EVENT_DELETE, &rootDeletes);
    lv_obj_add_event_cb(f.oldPopup, observedDelete, LV_EVENT_DELETE, &popupDeletes);
    lv_obj_add_event_cb(f.oldPopup, observedDelete, LV_EVENT_DELETE, &popupDeletes);
    f.deleteOldOnRelease = true;
    screen.hide();
    check(rootDeletes == 2 && popupDeletes == 2,
          "release callback tree deletion skipped unrelated DELETE observers");
    check(screen.visible() && screen.root() == f.replacement && topPopup(screen),
          "retired clear deleted a replacement after Host deleted old root/popup");
    screen.hide();
    check(f.releases == 2, "Host-deleted old tree double-freed an owned wallpaper");
    pump(2); expectNoTree(baseline);
  }

  std::puts("Lock screen: borrowed/owned wallpaper and cache, clock/unread cadence, Pager layout, "
            "external DELETE, countdown, and Host reentry passed.");
}
