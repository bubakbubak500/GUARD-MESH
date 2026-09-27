// SPDX-License-Identifier: GPL-3.0-or-later
#include "LockScreen.h"

#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

LockScreen::LockScreen(Host host) : _host(host) {}
LockScreen::~LockScreen() {
  _destroying = true;
  hide();
}

bool LockScreen::still(uint32_t generation) const {
  return !_destroying && _generation == generation && _root.get();
}
bool LockScreen::read(Snapshot &out) {
  out = Snapshot{};
  if (!_host.readSnapshot || !_host.readSnapshot(_host.context, out)) return false;
  out.clockText[sizeof out.clockText - 1] = 0;
  out.unlockHint[sizeof out.unlockHint - 1] = 0;
  if (out.minute < 0 || out.minute > 59) out.minute = 0;
  return true;
}

LockScreen::Wallpaper LockScreen::retireWallpaper() {
  // LVGL caches the descriptor address, not just the pixel pointer. Retire its
  // entry before either the storage buffer or this descriptor can be reused.
  lv_img_cache_invalidate_src(&_wallDescriptor);
  const Wallpaper old = _wallpaper;
  _wallpaper = Wallpaper{};
  std::memset(&_wallDescriptor, 0, sizeof _wallDescriptor);
  return old;
}

void LockScreen::clearPopup(bool deleting) {
  lv_obj_t *popup = _popup.get();
  _popup.set(nullptr);
  _count.set(nullptr);
  if (!deleting && popup) {
    lv_obj_remove_event_cb_with_user_data(popup, popupDeleted, this);
    lv_obj_del(popup);
  }
}

void LockScreen::clearRoot(bool deleting) {
  const uint32_t generation = ++_generation;
  lv_obj_t *root = _root.get();
  lv_obj_t *popup = _popup.get();
  // The Host may synchronously delete either retired widget while freeing a
  // decoded wallpaper. Watch their actual LVGL lifetime, not raw addresses.
  widgets::ObjectRef retiredRoot, retiredPopup;
  if (!deleting && root)
    lv_obj_remove_event_cb_with_user_data(root, rootDeleted, this);
  if (popup)
    lv_obj_remove_event_cb_with_user_data(popup, popupDeleted, this);
  const bool rootTracked = !root || deleting || retiredRoot.set(root);
  const bool popupTracked = !popup || retiredPopup.set(popup);
  _root.set(nullptr);
  _clock.set(nullptr); _unread.set(nullptr); _status.set(nullptr); _hint.set(nullptr);
  _popup.set(nullptr); _count.set(nullptr);
  _minute = -1; _quality = -1; _unreadCount = -1; _lastUnreadMs = 0;
  const Wallpaper oldWallpaper = retireWallpaper();
  // If LVGL cannot allocate a temporary DELETE watcher, delete that old object
  // now; after a Host callback its raw address would no longer be trustworthy.
  if (popup && !popupTracked) lv_obj_del(popup);
  if (!deleting && root && !rootTracked) lv_obj_del(root);
  if (oldWallpaper.owned && oldWallpaper.pixels && _host.releaseWallpaper)
    _host.releaseWallpaper(_host.context, oldWallpaper.pixels);
  if (lv_obj_t *oldPopup = retiredPopup.get()) lv_obj_del(oldPopup);
  if (lv_obj_t *oldRoot = retiredRoot.get()) lv_obj_del(oldRoot);
  // A Host or later DELETE observer may have opened a replacement. Its call to
  // statusBarTransparent(true) wins over this retired root's restoration.
  if (_generation == generation && _host.statusBarTransparent)
    _host.statusBarTransparent(_host.context, false);
}

void LockScreen::hide() {
  if (!_root.get() && !_popup.get() && !_wallpaper.pixels) return;
  clearRoot(false);
}
void LockScreen::rootDeleted(lv_event_t *event) {
  auto *self = static_cast<LockScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->clearRoot(true);
}
void LockScreen::popupDeleted(lv_event_t *event) {
  auto *self = static_cast<LockScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->clearPopup(true);
}

void LockScreen::applyClock(const Snapshot &page) {
  lv_obj_t *clock = _clock.get();
  if (!clock) return;
  lv_label_set_text(clock, page.clockText);
  lv_obj_set_style_text_color(clock,
      lv_color_hex(page.currentQuality ? page.textColor : colors().COLOR_STATUS_WARN),
      LV_PART_MAIN);
  const int dx = (page.minute % 5) * 3 - 6;
  const int dy = ((page.minute / 5) % 3) * 4 - 4;
  if (_layout == Layout::Pager)
    lv_obj_align(clock, LV_ALIGN_TOP_LEFT, 6 + dx, 30 + dy);
  else {
    lv_obj_align(clock, LV_ALIGN_TOP_MID, dx, 30 + dy);
    if (_unread.get()) lv_obj_align(_unread.get(), LV_ALIGN_TOP_MID, dx, 68 + dy);
  }
  _minute = page.minute;
  _quality = page.currentQuality ? 1 : 0;
}
void LockScreen::applyUnread(const Snapshot &page) {
  lv_obj_t *unread = _unread.get();
  if (!unread) return;
  _unreadCount = page.unread;
  if (page.unread <= 0) {
    lv_obj_add_flag(unread, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  char text[24];
  std::snprintf(text, sizeof text, LV_SYMBOL_ENVELOPE "  %d", page.unread);
  lv_label_set_text(unread, text);
  lv_obj_clear_flag(unread, LV_OBJ_FLAG_HIDDEN);
}

void LockScreen::show() {
  if (_destroying) return;
  if (lv_obj_t *existing = _root.get()) {
    const uint32_t generation = _generation;
    if (_host.statusBarTransparent)
      _host.statusBarTransparent(_host.context, true);
    if (still(generation) && _root.get() == existing)
      lv_obj_move_foreground(existing);
    return;
  }
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!root) return;
  if (!_root.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, this);
  const uint32_t generation = ++_generation;
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  if (_host.statusBarTransparent)
    _host.statusBarTransparent(_host.context, true);
  if (!still(generation) || _root.get() != root) return;

  Snapshot page;
  read(page);
  if (!still(generation) || _root.get() != root) return;
  _layout = page.layout;
  if (page.navSkipFlag) lv_obj_add_flag(root, page.navSkipFlag);

  Wallpaper incoming;
  const bool loaded = _host.loadWallpaper &&
                      _host.loadWallpaper(_host.context, incoming);
  if (!still(generation) || _root.get() != root) {
    if (incoming.owned && incoming.pixels && _host.releaseWallpaper)
      _host.releaseWallpaper(_host.context, incoming.pixels);
    return;
  }
  const bool valid = loaded && incoming.pixels && incoming.width && incoming.height &&
      static_cast<uint64_t>(incoming.width) * incoming.height * sizeof(lv_color_t) <= UINT32_MAX;
  if (!valid) {
    if (incoming.owned && incoming.pixels && _host.releaseWallpaper)
      _host.releaseWallpaper(_host.context, incoming.pixels);
  } else {
    _wallpaper = incoming;
    _wallDescriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    _wallDescriptor.header.w = incoming.width;
    _wallDescriptor.header.h = incoming.height;
    _wallDescriptor.data = incoming.pixels;
    _wallDescriptor.data_size = static_cast<uint32_t>(incoming.width) *
                                incoming.height * sizeof(lv_color_t);
    auto *image = lv_img_create(root);
    lv_img_set_src(image, &_wallDescriptor);
    lv_img_set_antialias(image, true);
    lv_img_set_pivot(image, incoming.width / 2, incoming.height / 2);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    uint32_t zx = static_cast<uint32_t>(sw) * 256u / incoming.width;
    uint32_t zy = static_cast<uint32_t>(sh) * 256u / incoming.height;
    uint32_t zoom = zx > zy ? zx : zy;
    if (zoom < 1) zoom = 1;
    if (zoom > 2048) zoom = 2048;
    lv_img_set_zoom(image, static_cast<uint16_t>(zoom));
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
  }
  if (!still(generation) || _root.get() != root) return;

  const lv_color_t textColor = lv_color_hex(page.textColor);
  auto *clock = lv_label_create(root); _clock.set(clock);
  lv_label_set_text(clock, "--:--");
  lv_obj_set_style_text_font(clock, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock, textColor, LV_PART_MAIN);
  if (_layout == Layout::Pager) {
    lv_obj_set_width(clock, 192);
    lv_obj_set_style_text_align(clock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(clock, LV_ALIGN_TOP_LEFT, 6, 30);
  } else lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, 30);

  auto *unread = lv_label_create(root); _unread.set(unread);
  lv_obj_set_style_text_font(unread, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(unread, textColor, LV_PART_MAIN);
  lv_obj_add_flag(unread, LV_OBJ_FLAG_HIDDEN);
  if (_layout != Layout::Pager) lv_obj_align(unread, LV_ALIGN_TOP_MID, 0, 68);

  auto *status = lv_label_create(root); _status.set(status);
  lv_label_set_text(status, TR("Screen locked"));
  lv_obj_set_style_text_font(status, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(status, textColor, LV_PART_MAIN);
  if (_layout != Layout::Pager) lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 190);

  auto *hint = lv_label_create(root); _hint.set(hint);
  useChainedFont(hint);
  lv_label_set_text(hint, page.unlockHint[0] ? page.unlockHint : TR("hold the trackball to unlock"));
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, textColor, LV_PART_MAIN);
  lv_obj_set_style_text_opa(hint, LV_OPA_70, LV_PART_MAIN);
  if (_layout == Layout::Pager) {
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
    lv_obj_align_to(status, hint, LV_ALIGN_OUT_TOP_RIGHT, 0, -4);
    lv_obj_align_to(unread, status, LV_ALIGN_OUT_TOP_RIGHT, 0, -4);
  } else lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
  applyClock(page);
  if (_host.readUnread) page.unread = _host.readUnread(_host.context);
  if (!still(generation) || _root.get() != root) return;
  applyUnread(page);
  _lastUnreadMs = page.nowMs;
}

void LockScreen::service() {
  if (!_root.get() || !_clock.get()) return;
  const uint32_t generation = _generation;
  Snapshot page;
  if (!read(page) || !still(generation)) return;
  if (page.minute != _minute || static_cast<int8_t>(page.currentQuality ? 1 : 0) != _quality)
    applyClock(page);
  if (static_cast<uint32_t>(page.nowMs - _lastUnreadMs) >= 1000u) {
    if (_host.readUnread) page.unread = _host.readUnread(_host.context);
    if (!still(generation)) return;
    _lastUnreadMs = page.nowMs;
    if (page.unread != _unreadCount) applyUnread(page);
  }
}

void LockScreen::hideUnlockProgress() { clearPopup(false); }
void LockScreen::showUnlockProgress(uint32_t remainingMs) {
  if (_destroying || !_root.get()) return;
  lv_obj_t *popup = _popup.get();
  if (!popup) {
    popup = lv_obj_create(lv_layer_top());
    if (!popup) return;
    if (!_popup.set(popup)) { lv_obj_del(popup); return; }
    lv_obj_add_event_cb(popup, popupDeleted, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(popup);
    lv_obj_set_size(popup, SC(180), SC(104));
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(popup, 12, LV_PART_MAIN);
    lv_obj_set_style_border_color(popup, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(popup, 1, LV_PART_MAIN);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    auto *title = lv_label_create(popup);
    lv_label_set_text(title, TR("Unlocking\xe2\x80\xa6"));
    lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &font16(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    auto *count = lv_label_create(popup); _count.set(count);
    lv_obj_set_style_text_color(count, lv_color_hex(colors().COLOR_STATUS_OK_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(count, &font16(), LV_PART_MAIN);
    lv_obj_align(count, LV_ALIGN_CENTER, 0, 6);
    auto *hint = lv_label_create(popup);
    lv_label_set_text(hint, TR("keep holding"));
    lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
  }
  lv_obj_move_foreground(popup);
  if (_count.get()) {
    char count[12];
    const unsigned seconds = remainingMs / 1000u + (remainingMs % 1000u != 0);
    std::snprintf(count, sizeof count, "%u", seconds);
    lv_label_set_text(_count.get(), count);
  }
}

} } // namespace ui::screens
