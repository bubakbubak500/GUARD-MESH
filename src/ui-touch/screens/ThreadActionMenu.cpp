// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThreadActionMenu.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
#include "ConfirmDialog.h"
#include <cstdio>
#include <cstring>
namespace ui {
namespace screens {
namespace threadMenu {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef muteMessages, muteMentions, shareBody;
static Thread selected{}, pending{}, iconThread{};
static uint32_t iconSerial = 0, iconRequest = 0;
static char secretHex[33] = {}, shareName[33] = {};
enum class Action {
  MarkRead,
  Remove,
  ClearHistory,
  Region,
  Share,
  Blocked,
  Login,
  Join,
  ResetPath,
  MuteMessages,
  MuteMentions,
  Icon,
  ResetIcon
};
static Action pendingAction = Action::Remove;
static void iconPicked(uint32_t, const char *);
static ConfirmDialog confirmation({[]() -> lv_coord_t { return host.statusHeight(); },
                                   [](lv_obj_t **object) { host.closeRoot(object); },
                                   [](lv_obj_t *object) {
                                     if (host.focusConfirm)
                                       host.focusConfirm(object);
                                   }});
static bool belongs(lv_obj_t *object, lv_obj_t *owner) {
  for (; owner && object; object = lv_obj_get_parent(object))
    if (object == owner)
      return true;
  return false;
}
static bool owns(lv_event_t *event) { return belongs(lv_event_get_current_target(event), root); }
static bool resolve(const Thread &captured, Thread &live) {
  return captured.index >= 0 && host.read && host.read(captured.index, live) &&
         captured.channel == live.channel && !strcmp(captured.name, live.name) &&
         captured.hasContact == live.hasContact &&
         (!captured.hasContact || !memcmp(captured.contact, live.contact, 32)) &&
         captured.hasSecret == live.hasSecret &&
         (!captured.hasSecret || !memcmp(captured.secret, live.secret, 16));
}
static void closeMenu() {
  selected = Thread{};
  muteMessages.set(nullptr);
  muteMentions.set(nullptr);
  if (root)
    host.closeRoot(&root);
}
void close() {
  iconRequest = 0;
  iconThread = Thread{};
  pending = Thread{};
  if (host.cancelIcon)
    host.cancelIcon(iconPicked);
  confirmation.dismiss();
  closeMenu();
  auto *body = shareBody.get();
  shareBody.set(nullptr);
  if (body && host.closeShare)
    host.closeShare(body);
  memset(secretHex, 0, sizeof secretHex);
}
void configure(const Host &value) {
  close();
  host = value;
}
bool isOpen() { return root || confirmation.isOpen(); }
static void rootDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) != root)
    return;
  root = nullptr;
  selected = Thread{};
  iconRequest = 0;
  muteMessages.set(nullptr);
  muteMentions.set(nullptr);
  if (host.cancelIcon)
    host.cancelIcon(iconPicked);
}
static void channelLongSheetDismissCb(lv_event_t *event) {
  if (owns(event))
    close();
}
static void channelShareCopyCb(lv_event_t *event) {
  const auto code = lv_event_get_code(event);
  if ((code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED) &&
      belongs(lv_event_get_current_target(event), shareBody.get()) && host.shareActive(shareBody.get()))
    host.copySecret(secretHex);
}
static void chmuteRefreshLabels() {
  Thread live{};
  if (!resolve(selected, live))
    return;
  const uint8_t flags = host.mute(live.name);
  if (auto *button = muteMessages.get())
    if (auto *label = lv_obj_get_child(button, 0))
      lv_label_set_text_fmt(label, "%s  %s", (flags & 1) ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO,
                            (flags & 1) ? TR("Unmute msgs") : TR("Mute msgs"));
  if (auto *button = muteMentions.get())
    if (auto *label = lv_obj_get_child(button, 0))
      lv_label_set_text_fmt(label, "%s  %s", (flags & 2) ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO,
                            (flags & 2) ? TR("Unmute @") : TR("Mute @"));
}
static void iconPicked(uint32_t request, const char *glyph) {
  if (!request || request != iconRequest || !root || !glyph)
    return;
  iconRequest = 0;
  Thread live{};
  if (resolve(iconThread, live)) {
    host.setIcon(live.name, glyph);
    host.alert(TR("Chat icon set"), 1200);
  }
}
static void confirmed() {
  const Thread captured = pending;
  const Action action = pendingAction;
  pending = Thread{};
  Thread live{};
  if (!resolve(captured, live))
    return;
  if (action == Action::Remove)
    host.erase(live);
  else if (action == Action::ClearHistory)
    host.clearHistory(live.index);
}
static void openChannelShareModal(const char *, const uint8_t *);
static void dispatch(lv_event_t *event, Action action) {
  if (!owns(event))
    return;
  Thread live{};
  if (!resolve(selected, live))
    return;
  if (action == Action::MuteMessages || action == Action::MuteMentions) {
    if (!live.channel)
      return;
    host.setMute(live.name, host.mute(live.name) ^ (action == Action::MuteMessages ? 1 : 2));
    chmuteRefreshLabels();
    return;
  }
  if (action == Action::Icon) {
    if (!live.channel)
      return;
    iconThread = live;
    iconRequest = ++iconSerial;
    if (!iconRequest)
      iconRequest = ++iconSerial;
    host.pickIcon(iconPicked, iconRequest);
    return;
  }
  if (action == Action::ResetIcon) {
    if (!live.channel)
      return;
    iconRequest = 0;
    host.cancelIcon(iconPicked);
    if (auto *input = lv_indev_get_act())
      lv_indev_wait_release(input);
    host.setIcon(live.name, "");
    host.alert(TR("Chat icon reset to letters"), 1400);
    return;
  }
  close();
  switch (action) {
  case Action::MarkRead:
    host.markRead(live.index);
    host.alert(TR("Marked read"), 900);
    break;
  case Action::Remove:
  case Action::ClearHistory: {
    pending = live;
    pendingAction = action;
    char message[128];
    if (action == Action::ClearHistory)
      snprintf(message, sizeof message, "%s",
               TR("Delete this chat's entire history?\nThe chat itself stays."));
    else if (live.channel)
      snprintf(message, sizeof message, TR("Remove channel \"%s\"?\nLeaves it on this device only."),
               live.name);
    else
      snprintf(message, sizeof message, TR("Delete chat with \"%s\"?\nMessage history will be cleared."),
               live.name);
    if (host.prepareConfirm)
      host.prepareConfirm();
    confirmation.show(message, live.channel && action == Action::Remove ? TR("Remove") : TR("Delete"),
                      confirmed, false);
    break;
  }
  case Action::Region:
    if (live.channel && live.channelSlot >= 0)
      host.scope(live.channelSlot, live.name);
    else
      host.alert(TR("Channel not found"), 1200);
    break;
  case Action::Share:
    if (live.channel && live.hasSecret)
      openChannelShareModal(live.name, live.secret);
    else
      host.alert(TR("Channel not found"), 1200);
    break;
  case Action::Blocked:
    host.blocked();
    break;
  case Action::Login:
    if (live.hasContact)
      host.login(live.contact);
    else
      host.alert(TR("Couldn't send login"), 1400);
    break;
  case Action::Join:
    if (live.hasContact)
      host.join(live.contact);
    else
      host.alert(TR("Contact gone"), 1200);
    break;
  case Action::ResetPath:
    if (live.hasContact)
      host.resetPath(live.contact);
    else
      host.alert(TR("Path reset failed"), 1200);
    break;
  default:
    break;
  }
}
template <Action action> static void clicked(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    dispatch(event, action);
}
static void threadSheetIconResetCb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED)
    dispatch(event, Action::ResetIcon);
}

static void openChannelShareModal(const char *channel_name, const uint8_t secret[16]) {
  // Render hex once and stash it for the Copy button.
  for (int i = 0; i < 16; ++i) {
    snprintf(secretHex + i * 2, 3, "%02x", secret[i]);
  }
  strncpy(shareName, channel_name ? channel_name : "Channel", sizeof(shareName) - 1);

  lv_obj_t *body = host.shareBody();
  if (!body || !shareBody.set(body))
    return;
  int y = 0;

  lv_obj_t *name_l = lv_label_create(body);
  lv_label_set_text_fmt(name_l, LV_SYMBOL_LOOP "  %s", shareName);
  lv_obj_set_style_text_color(name_l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &font14(), LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 26;

  lv_obj_t *hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_label_set_text(hint, TR("32-hex secret. Anyone with this can read the channel."));
  lv_obj_set_pos(hint, 2, y);
  y += 32;

  lv_obj_t *sec_lbl = lv_label_create(body);
  lv_label_set_long_mode(sec_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sec_lbl, lv_pct(100));
  lv_label_set_text(sec_lbl, secretHex);
  lv_obj_set_style_text_color(sec_lbl, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec_lbl, &font14(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sec_lbl, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sec_lbl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sec_lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_radius(sec_lbl, 4, LV_PART_MAIN);
  lv_obj_set_pos(sec_lbl, 2, y);
  // Long-press copies too — handy if the Copy button is hidden by the keyboard.
  lv_obj_add_flag(sec_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sec_lbl, channelShareCopyCb, LV_EVENT_LONG_PRESSED, nullptr);
  y += 52;

  lv_obj_t *b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100), 36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_STATUS_OK_PRESSED),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(b, channelShareCopyCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(b);
  useChainedFont(bl);
  lv_label_set_text(bl, TR("Copy secret"));
  lv_obj_center(bl);
}

void show(int thread_idx) {
  Thread captured{};
  if (!host.read || !host.read(thread_idx, captured))
    return;
  close();
  selected = captured;
  const bool is_channel = selected.channel;
  const char *name = selected.name;

  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  // Backdrop sits below the global status bar — keeps the centered card
  // from being shoved partway behind the time/battery row and lets the
  // bar stay readable while the sheet is open.
  lv_obj_set_size(root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr) - host.statusHeight());
  lv_obj_set_pos(root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(root);
  lv_obj_add_event_cb(root, channelLongSheetDismissCb, LV_EVENT_CLICKED, nullptr);

  // Same metrics as the contact action sheet — the two sheets are siblings and
  // should read identically. (PSC is a no-op on the smaller boards.)
#if defined(TLORA_PAGER)
  // The Pager is twice as wide as the 240/320-px touch layouts but substantially
  // shorter. Spend that width: fixed-size one-line labels and shorter rows keep
  // the complete channel menu visible at every text preset.
  const int card_w = lv_disp_get_hor_res(nullptr) - 80;
  const int btn_h = 26;
  const int btn_gap = 3;
  const int title_h = 32; // reserve the full close-X focus/hit target
  const int pad = 6;
  const lv_font_t *row_font = uiChromeFont();
#elif CAP_LARGE_SCREEN
  const int card_w = PCW(232);
  const int btn_h = PSC(30);
  const int btn_gap = PSC(6);
  const int title_h = PSC(28);
  const int pad = PSC(6);
  const lv_font_t *row_font = &font14();
#else
  const int card_w = 232;
  const int btn_h = 30;
  const int btn_gap = 4; // 4 (was 6): the 7-item channel grid + danger row must fit the T-Deck's ~206 px
  const int title_h = 28;
  const int pad = 6;
  const lv_font_t *row_font = &font12();
#endif
  // Room-server thread? Adds the "Log in again" row (issue #89 session recovery).
  const bool is_room_thread = selected.room;
  // Everything except Remove/Delete goes in the 2-column grid; the danger row
  // spans the full width at the bottom. Channel: mark-read + region + the two
  // mutes + share + blocked + chat icon + delete history = 8 grid items
  // (4 rows). Room: mark-read + login-again + join + reset-path + blocked +
  // delete history = 6 (3 rows — the join row fills what was a half-empty
  // bottom row, so the card height is unchanged). DM: mark-read + reset-path
  // + blocked + delete history = 4 (2 rows).
  const int grid_items = is_channel ? 8 : (is_room_thread ? 6 : 4);
  const int grid_rows = (grid_items + 1) / 2; // ceil
  // Exact content height: outer padding + fixed header + grid rows + one
  // full-width danger row. There is no trailing gap after the danger row.
  int card_h = 2 * pad + title_h + (grid_rows + 1) * btn_h + grid_rows * btn_gap;
  const int max_h = lv_disp_get_ver_res(nullptr) - host.statusHeight() - 12;
  const bool card_scroll = (card_h > max_h); // safety net only — all three variants fit both S3 boards
  if (card_scroll)
    card_h = max_h;
  muteMessages.set(nullptr);
  muteMentions.set(nullptr);
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
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE); // header stays fixed; the body below scrolls
  addCloseXBadge(card, channelLongSheetDismissCb);

  lv_obj_t *title = lv_label_create(card);
  char nm[40];
  host.sanitize(&font14(), nm, sizeof(nm), name ? name : "");
  lv_label_set_text_fmt(title, "%s  %s", is_channel ? LV_SYMBOL_LOOP : LV_SYMBOL_ENVELOPE,
                        nm[0] ? nm : (is_channel ? "(channel)" : "(chat)"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  // Trim 32 px on the right so long channel names don't slide under the X.
  lv_obj_set_width(title, card_w - 2 * pad - 32);
  lv_obj_set_pos(title, 0, 0);

  // Scrollable body BELOW the fixed title + X header (scroll is a fallback that
  // shouldn't trigger — the grid fits). Buttons are clipped to the body, so the
  // floating X is never drawn on top of one.
  lv_obj_t *body = lv_obj_create(card);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 0, title_h);
  lv_obj_set_size(body, card_w - 2 * pad, card_h - 2 * pad - title_h);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  if (card_scroll)
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
  else
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  const int col_gap = btn_gap;
  const int half_w = (card_w - 2 * pad - col_gap) / 2;
  int y = 0;
  int col = 0; // 0 = left column, 1 = right column
  // Half-width grid button; advances column, wrapping to the next row. Labels
  // get DOT-ellipsis so a long translation ("Geblokkeerde gebruikers") degrades
  // gracefully instead of hard-clipping at the cell edge.
  auto mk = [&](const char *lbl, lv_event_cb_t cb, uint32_t bg) -> lv_obj_t * {
    lv_obj_t *b = lv_btn_create(body);
    lv_obj_set_size(b, half_w, btn_h);
    lv_obj_set_pos(b, col == 0 ? 0 : (half_w + col_gap), y);
    styleButton(b);
    lv_obj_set_style_pad_ver(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 4, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, TR(lbl));
    lv_obj_set_style_text_font(l, row_font, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    // LONG_DOT only stays on one line when height is constrained. Previously a
    // Large-preset label wrapped into a second line and escaped its 30-px row.
    lv_obj_set_size(l, half_w - 8, lv_font_get_line_height(row_font));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(l);
    if (col == 0)
      col = 1;
    else {
      col = 0;
      y += btn_h + btn_gap;
    }
    return b;
  };
  // Full-width danger row (Remove/Delete). Closes any half-open grid row first.
  auto mk_full = [&](const char *lbl, lv_event_cb_t cb, uint32_t bg) {
    if (col == 1) {
      col = 0;
      y += btn_h + btn_gap;
    }
    lv_obj_t *b = lv_btn_create(body);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_set_style_pad_ver(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 8, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_bg_color(b, lv_color_hex(themeRole(bg, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, TR(lbl));
    lv_obj_set_style_text_font(l, row_font, LV_PART_MAIN);
    if (bg)
      lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_ON_STATUS_DANGER), LV_PART_MAIN);
    lv_obj_center(l);
    y += btn_h + btn_gap;
  };
  mk(TR(LV_SYMBOL_OK "  Mark as read"), clicked<Action::MarkRead>, 0);
  if (is_channel) {
    mk(TR(LV_SYMBOL_SETTINGS "  Region & scope"), clicked<Action::Region>, 0);
    // The two mutes sit side by side on their own row; labels set by chmuteRefreshLabels().
    muteMessages.set(mk("", clicked<Action::MuteMessages>, 0));
    muteMentions.set(mk("", clicked<Action::MuteMentions>, 0));
    chmuteRefreshLabels();
    mk(TR(LV_SYMBOL_SHUFFLE "  Share secret"), clicked<Action::Share>, 0);
    mk(TR(LV_SYMBOL_CLOSE "  Blocked users"), clicked<Action::Blocked>, 0);
    // Chat-list avatar emoji: tap = pick, long-press = back to the two-letter auto avatar.
    lv_obj_t *icb = mk(TR(LV_SYMBOL_IMAGE "  Chat icon"), clicked<Action::Icon>, 0);
    if (icb)
      lv_obj_add_event_cb(icb, threadSheetIconResetCb, LV_EVENT_LONG_PRESSED, nullptr);
    mk(TR(LV_SYMBOL_TRASH "  Delete history"), clicked<Action::ClearHistory>, 0);
    mk_full(TR(LV_SYMBOL_TRASH "  Remove channel"), clicked<Action::Remove>, 0xB23A48);
  } else {
    if (is_room_thread) {
      mk(TR(LV_SYMBOL_REFRESH "  Log in again"), clicked<Action::Login>, 0);
      mk(TR(LV_SYMBOL_LOOP "  Join w/ password"), clicked<Action::Join>, 0);
    }
    mk(TR(LV_SYMBOL_LOOP "  Reset path"), clicked<Action::ResetPath>, 0);
    mk(TR(LV_SYMBOL_CLOSE "  Blocked users"), clicked<Action::Blocked>, 0);
    mk(TR(LV_SYMBOL_TRASH "  Delete history"), clicked<Action::ClearHistory>, 0);
    mk_full(TR(LV_SYMBOL_TRASH "  Delete chat"), clicked<Action::Remove>, 0xB23A48);
  }
}

} // namespace threadMenu
} // namespace screens
} // namespace ui
