// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageActionMenu.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include "ConfirmDialog.h"
#include <cstdio>
#include <cstring>
namespace ui {
namespace screens {
namespace messageMenu {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static Message selected{};
static int selectedIndex = -1;
static ConfirmDialog retryDialog({[]() -> lv_coord_t { return host.statusHeight(); },
                                  [](lv_obj_t **object) { host.closeRoot(object); },
                                  [](lv_obj_t *object) {
                                    if (host.focusConfirmation)
                                      host.focusConfirmation(object);
                                  }});
enum class Action { Ack, Mention, Copy, Info, Block, Resend, Delete };
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void rootDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) == root) {
    root = nullptr;
    selectedIndex = -1;
  }
}
void close() {
  selectedIndex = -1;
  retryDialog.dismiss();
  if (root)
    host.closeRoot(&root);
}
bool isOpen() { return root != nullptr || retryDialog.isOpen(); }
void configure(const Host &value) {
  close();
  host = value;
}
static void retryConfirmed() {
  const Message message = selected;
  const int index = selectedIndex;
  selectedIndex = -1;
  Message live{};
  if (host.readMessage(index, live) && live.seq == message.seq && host.activeConversation(message))
    host.resend(message.text);
}
void retry(int index) {
  Message message{};
  if (!host.readMessage || !host.readMessage(index, message) || !host.activeConversation(message) ||
      !message.outgoing || message.deliv_state != MessageTypes::DELIV_FAILED || !message.text[0])
    return;
  close();
  if (host.prepareRetry)
    host.prepareRetry();
  selected = message;
  selectedIndex = index;
  retryDialog.show(TR("Message didn't get through.\nResend it?"), TR("Resend"), retryConfirmed, false);
}
static void backdropEvent(lv_event_t *event) {
  if (owns(event))
    close();
}
static void buildAckText(MessageTypes::UIMessage &m, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  int n = 0;
  if (m.channel && m.sender[0])
    n += snprintf(out + n, cap - (size_t)n, "@[%s], ack", m.sender);
  else
    n += snprintf(out + n, cap - (size_t)n, "Ack");
  if (m.meta_flags & MessageTypes::MSG_META_HAS_RX) {
    n +=
        snprintf(out + n, cap - (size_t)n, ": SNR %.1f dB, RSSI %d dBm", (double)m.snr_q4 / 4.0, (int)m.rssi);
    if (m.meta_flags & MessageTypes::MSG_META_IS_FLOOD) {
      const uint8_t hops = (uint8_t)(m.path_len & 0x3F);
      n += snprintf(out + n, cap - (size_t)n, ", %u hop%s", (unsigned)hops, hops == 1 ? "" : "s");
      // Append the hop codes in hex ("via 3A, F1, …") — the raw path-hash bytes
      // each repeater is keyed by, matching what the mesh shows on the wire.
      const uint8_t hsz = (uint8_t)((m.path_len >> 6) + 1);
      int off = 0, listed = 0;
      for (uint8_t h = 0; h < hops && off + (int)hsz <= m.in_path_n && listed < 8; ++h, off += hsz) {
        if (n >= (int)cap - 24)
          break; // leave room; never overflow out[]
        n += snprintf(out + n, cap - (size_t)n, "%s", listed == 0 ? " via " : ", ");
        for (uint8_t b = 0; b < hsz && b < 4 && n < (int)cap - 3; ++b)
          n += snprintf(out + n, cap - (size_t)n, "%02X", m.in_path[off + b]);
        ++listed;
      }
    } else {
      n += snprintf(out + n, cap - (size_t)n, ", direct");
    }
  }
  if (n < 0 || n >= (int)cap)
    out[cap - 1] = '\0';
}

static void actionEvent(lv_event_t *event) {
  if (!owns(event))
    return;
  const Message message = selected;
  const int index = selectedIndex;
  const auto action = static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  Message live{};
  const bool current =
      host.readMessage(index, live) && live.seq == message.seq && host.activeConversation(message);
  close();
  if (!current)
    return;
  if (auto *input = lv_indev_get_act())
    lv_indev_wait_release(input);
  switch (action) {
  case Action::Copy:
    host.copy(message.text);
    break;
  case Action::Info:
    host.info(index);
    break;
  case Action::Mention: {
    char text[MessageTypes::MAX_SENDER_NAME + 8];
    snprintf(text, sizeof text, "@[%s] ", message.sender);
    host.insert(true, text);
    break;
  }
  case Action::Ack: {
    Message copy = message;
    char text[MessageTypes::MAX_MSG_TEXT + 1]{};
    buildAckText(copy, text, sizeof text);
    host.insert(message.channel, text);
    break;
  }
  case Action::Block:
    host.block(message.sender);
    break;
  case Action::Resend:
    host.resend(message.text);
    break;
  case Action::Delete:
    host.erase(index);
    break;
  }
}
void show(int index) {
  Message m{};
  if (!host.readMessage || !host.readMessage(index, m) || !host.activeConversation(m))
    return;
  close();
  host.closeInfo();
  selected = m;
  selectedIndex = index;
  // "Ack" (quick link-quality confirmation) + "Mention" are for incoming messages.
  const bool can_ack = !m.outgoing && m.sender[0]; // ack its sender (channel or DM)
  const bool can_mention = m.channel && !m.outgoing && m.sender[0];
  const bool can_block = !m.outgoing;              // block the sender — never for our own messages
  const bool can_resend = m.outgoing && m.text[0]; // re-send one of OUR messages (DM or channel)

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  // Sit below the global status bar so the centered card lines up with the
  // visible area (matches every other modal in the UI).
  lv_obj_set_size(root, sw, sh - host.statusHeight());
  lv_obj_set_pos(root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, backdropEvent, LV_EVENT_CLICKED, nullptr);

  // The 800×480 Tanmatsu dwarfs this menu, so widen it and grow the rows; the
  // smaller boards keep plain integers (PSC is a no-op there). Buttons sit TWO
  // per row — with Delete the menu carries up to 7 actions, and a single column
  // no longer fit the 240-px screens without scrolling.
#if defined(TLORA_PAGER)
  // Spend the Pager's horizontal room instead of its scarce vertical room.
  const int card_w = sw - 80;
  const int btn_h = 30;
  const int gap = 6;
  const int pad = 10;
  const int hdr_h = 32; // keep the first row below the full close-X target
#elif CAP_LARGE_SCREEN
  const int card_w = PCW(220);
  const int btn_h = PSC(30);
  const int gap = PSC(4);
  const int pad = PSC(10);
  const int hdr_h = PSC(24);
#else
  const int card_w = 200;
  const int btn_h = 30;
  const int gap = 4;
  const int pad = 10;
  // Header row reserves space for the close-X badge so it doesn't sit on a button.
  const int hdr_h = 24;
#endif
  const int nbtn = (can_ack ? 1 : 0) + (can_mention ? 1 : 0) + 3 /*Copy+Info+Delete*/ + (can_block ? 1 : 0) +
                   (can_resend ? 1 : 0);
  const int nrows = (nbtn + 1) / 2;
  int card_h = hdr_h + nrows * btn_h + (nrows - 1) * gap + 2 * pad;
  // Never exceed the visible area under the status bar; scroll if it ever would
  // (e.g. an even taller menu, or a shorter display). The close-X floats, so it
  // stays pinned while the buttons scroll.
  const int avail_h = (int)(sh - host.statusHeight()) - 8;
  const bool scroll_menu = (card_h > avail_h);
  if (scroll_menu)
    card_h = avail_h;

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
  if (scroll_menu) {
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  } else {
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  }
  addCloseXBadge(card, backdropEvent);

  // Two buttons per row, filled in reading order; an odd last button gets its
  // own row (left cell). bi advances per button; the grid math places it.
  const int bw = (card_w - 2 * pad - gap) / 2;
  int bi = 0;
  auto mk_btn = [&](const char *text, Action action) {
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, bw, btn_h);
    lv_obj_set_pos(b, (bi % 2) * (bw + gap), hdr_h + (bi / 2) * (btn_h + gap));
    styleButton(b);
    lv_obj_add_event_cb(b, actionEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(action)));
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, TR(text));
    lv_obj_set_style_text_font(lbl, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    uiFitLabelWidth(lbl,
                    bw - 8); // shrink a long translation (e.g. FR "Supprimer") to fit the fixed-width button
    lv_obj_center(lbl);
    ++bi;
  };
  if (can_ack)
    mk_btn(TR(LV_SYMBOL_OK "  Ack"), Action::Ack);
  if (can_mention) {
    char ml[MessageTypes::MAX_SENDER_NAME + 16];
#if defined(TLORA_PAGER)
    snprintf(ml, sizeof ml, "@%.16s", m.sender); // wide Pager cells can keep more of the name
#else
    snprintf(ml, sizeof ml, "@%.10s", m.sender); // "Mention" is implied by the @; half-width cell
#endif
    mk_btn(ml, Action::Mention);
  }
  mk_btn(TR(LV_SYMBOL_COPY "  Copy"), Action::Copy);
  mk_btn(TR(LV_SYMBOL_LIST "  Info"), Action::Info);
  if (can_block)
    mk_btn(TR(LV_SYMBOL_CLOSE "  Block"), Action::Block);
  if (can_resend)
    mk_btn(TR(LV_SYMBOL_REFRESH "  Resend"), Action::Resend);
  mk_btn(TR(LV_SYMBOL_TRASH "  Delete"), Action::Delete);
}

} // namespace messageMenu
} // namespace screens
} // namespace ui
