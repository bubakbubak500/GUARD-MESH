// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickReplyPicker.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../models/MessageTypes.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <stdint.h>
namespace ui {
namespace screens {
namespace quickReplyPicker {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef target;
static constexpr size_t ReplyBytes = MessageTypes::MAX_MSG_TEXT + 1;
static char *replies = nullptr;
static int replyCount = 0;
static void targetDeleted(lv_event_t *);
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void clearState() {
  if (target.get())
    lv_obj_remove_event_cb(target.get(), targetDeleted);
  target.set(nullptr);
  platform::release(replies);
  replies = nullptr;
  replyCount = 0;
}
void close() {
  clearState();
  if (root)
    host.closeRoot(&root);
}
bool isOpen() { return root != nullptr; }
void configure(const Host &value) {
  close();
  host = value;
}
void cancelFor(lv_obj_t *object) {
  if (object && target.get() == object)
    close();
}
static void targetDeleted(lv_event_t *) { close(); }
static void rootDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) == root) {
    root = nullptr;
    clearState();
  }
}
static void closeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && owns(event))
    close();
}
static void insert(const char *text) {
  ObjectRef destination;
  destination.set(target.get());
  close();
  if (destination.get() && text && *text)
    host.insert(destination.get(), text);
}
static void replyEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !owns(event))
    return;
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (index < 0 || index >= replyCount)
    return;
  char text[ReplyBytes];
  memcpy(text, replies + size_t(index) * ReplyBytes, ReplyBytes);
  insert(text);
}
static void positionEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !owns(event))
    return;
  double latitude = 0, longitude = 0;
  char text[48] = {};
  if (host.position(latitude, longitude))
    snprintf(text, sizeof text, "%.5f, %.5f", latitude, longitude);
  insert(text);
}

// Build + show the quick-reply (macro) picker for a chat panel. Shared by the composer's
// △ chip tap (openQuickReplyPickerCb) and the orange F2 hardware key on the Tanmatsu.
void open(lv_obj_t *object) {
  close();
  if (!object || !target.set(object))
    return;
  lv_obj_add_event_cb(object, targetDeleted, LV_EVENT_DELETE, nullptr);
  replyCount = host.replyCount();
  if (replyCount < 0)
    replyCount = 0;
  if (size_t(replyCount) > SIZE_MAX / ReplyBytes) {
    close();
    return;
  }
  if (replyCount) {
    replies = static_cast<char *>(platform::allocate(size_t(replyCount) * ReplyBytes, true));
    if (!replies) {
      close();
      return;
    }
    memset(replies, 0, size_t(replyCount) * ReplyBytes);
    for (int i = 0; i < replyCount; ++i) {
      auto *text = replies + size_t(i) * ReplyBytes;
      if (host.readReply(i, text, ReplyBytes) <= 0)
        text[0] = 0;
      text[ReplyBytes - 1] = 0;
    }
  }
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  // Sit below the status bar — the QR card (~290 px tall with 6 macro
  // rows) was getting its top row clipped behind the time/battery row.
  lv_obj_set_size(root, sw, sh - host.statusHeight());
  lv_obj_set_pos(root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, closeEvent, LV_EVENT_CLICKED, nullptr);

  // Bigger on the 800-px Tanmatsu panel; unchanged on the smaller boards.
  // The pager's 480-wide landscape panel has plenty of spare width next to the
  // old 220px single-column card (reported: half the screen sat empty) — give
  // it 2 columns and a wider card instead of the shared 1-column sizing.
#if CAP_LARGE_SCREEN
  const int card_w = PCW(210);
  const int btn_h = SC(32); // SC not PSC: the 1.7x PSC boost made this 6-row card taller than the screen
  const int pad = SC(8);
  const int title_h = SC(26);
  const int hint_h = SC(22);
  const int row_gap = SC(4);
  const int cols = 1;
  const int col_gap = 0;
#elif defined(TLORA_PAGER)
  const int card_w = 360;
  // Six replies fit in three compact rows. Keep the picker entirely inside
  // the 200-px viewport instead of relying on a height clamp whose absolutely
  // positioned footer was only reachable by scrolling past the card edge.
  const int btn_h = 30;
  const int pad = 6;
  const int title_h = 24;
  const int hint_h = 18;
  const int row_gap = 3;
  const int cols = 2;
  const int col_gap = 6;
#else
  const int card_w = 220;
  const int btn_h = 32;   // 34→32: 6 macro rows have to fit in the
  const int pad = 8;      // visible area (298 px) below the status
  const int title_h = 26; // bar — the old sizing produced a 302 px
  const int hint_h = 22;  // card that clipped behind the bar.
  const int row_gap = 4;
  const int cols = 1;
  const int col_gap = 0;
#endif
  const int col_w = (card_w - 2 * pad - (cols - 1) * col_gap) / cols;
  const int rows = (replyCount + cols - 1) / cols; // ceil, in case the macro count ever changes
  const int gps_row_h = btn_h + row_gap; // GPS-position row: always full-width, sits above the macro grid
  int card_h = title_h + gps_row_h + rows * (btn_h + row_gap) + hint_h + pad;
#if defined(TLORA_PAGER)
  card_h += pad; // both top and bottom content padding are inside the fixed-height card
#endif
  if (card_h > sh - host.statusHeight() - 8)
    card_h = sh - host.statusHeight() - 8; // never taller than the visible area
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
#if defined(HAS_M9_KEYBOARD)
  // The focus glow extends 8 px beyond the row. Keep it inside the clipped
  // scroll viewport when LVGL aligns the first reply to the top padding.
  lv_obj_set_style_pad_top(card, pad + 2, LV_PART_MAIN);
#endif
  // The card is clamped to the visible height above, but its rows are absolutely
  // positioned and can extend past that clamp (small screens + Large/Huge UI scale) —
  // enable vertical scrolling so the lower quick-replies + hint stay reachable (GH #151).
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  addCloseXBadge(card, closeEvent);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR("Quick reply"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
#if defined(TLORA_PAGER)
  lv_obj_set_style_text_font(title, uiChromeFont(), LV_PART_MAIN);
#else
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
#endif
  lv_obj_set_pos(title, 0, 0);

  { // --- My position (GPS) --- first row; greyed out without a live fix
    double latitude = 0, longitude = 0;
    const bool fix = host.position(latitude, longitude);
    char gbuf[64];
    if (fix)
      snprintf(gbuf, sizeof gbuf, LV_SYMBOL_GPS " %.5f, %.5f", latitude, longitude);
    else
      snprintf(gbuf, sizeof gbuf, LV_SYMBOL_GPS " %s", TR("My position (no GPS fix)"));
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, title_h);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(fix ? colors().COLOR_CONTROL : colors().COLOR_CONTROL_DISABLED),
                              LV_PART_MAIN);
    if (fix)
      lv_obj_add_event_cb(b, positionEvent, LV_EVENT_CLICKED, nullptr);
    else
      lv_obj_add_state(b, LV_STATE_DISABLED); // disabled = LVGL swallows the click
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, gbuf);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, card_w - 2 * pad - 16);
    lv_obj_set_style_text_font(lbl,
#if defined(TLORA_PAGER)
                               uiChromeFont(),
#else
                               &font12(),
#endif
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fix ? colors().COLOR_TEXT : colors().COLOR_SUB),
                                LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
  }
  for (int i = 0; i < replyCount; ++i) {
    char buf[ReplyBytes];
    snprintf(buf, sizeof buf, "%s", replies + size_t(i) * ReplyBytes);
    const int n = static_cast<int>(strlen(buf));
    if (n <= 0) {
      strncpy(buf, "(empty)", sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    }
    const int col = i % cols, row = i / cols;
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, col_w, btn_h);
    lv_obj_set_pos(b, col * (col_w + col_gap), title_h + gps_row_h + row * (btn_h + row_gap));
    styleButton(b);
    lv_obj_set_style_bg_color(
        b, lv_color_hex(n > 0 ? colors().COLOR_CONTROL : colors().COLOR_CONTROL_DISABLED), LV_PART_MAIN);
    lv_obj_add_event_cb(b, replyEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, buf);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, col_w - 16);
    lv_obj_set_style_text_font(lbl,
#if defined(TLORA_PAGER)
                               uiChromeFont(),
#else
                               &font12(),
#endif
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(n > 0 ? colors().COLOR_TEXT : colors().COLOR_SUB),
                                LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
  }
  const int y = title_h + gps_row_h + rows * (btn_h + row_gap); // hint sits below the last row
  lv_obj_t *hint = lv_label_create(card);
  lv_label_set_text(hint, TR("Edit in Settings \xe2\x86\x92 Quick replies"));
  lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
#if defined(TLORA_PAGER)
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
  lv_obj_set_width(hint, card_w - 2 * pad);
#else
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
#endif
  lv_obj_set_pos(hint, 0, y + 2);
}

} // namespace quickReplyPicker
} // namespace screens
} // namespace ui
