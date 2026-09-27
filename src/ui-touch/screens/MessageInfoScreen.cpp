// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageInfoScreen.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ChatText.h"
#include "../widgets/Styles.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace ui {
namespace screens {
namespace messageInfo {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static lv_obj_t *traceRoot = nullptr;
static lv_obj_t *bodyObject = nullptr;
static Message selected{};
static constexpr uint8_t ambiguousRegionSlot =
    MessageTypes::MSG_META_SCOPE_SLOT_MASK >> MessageTypes::MSG_META_SCOPE_SLOT_SHIFT;
static bool owns(lv_event_t *event, lv_obj_t *owner) {
  for (auto *object = lv_event_get_current_target(event); owner && object; object = lv_obj_get_parent(object))
    if (object == owner)
      return true;
  return false;
}
static void rootDeleted(lv_event_t *event) {
  auto **owner = static_cast<lv_obj_t **>(lv_event_get_user_data(event));
  if (lv_event_get_target(event) != *owner)
    return;
  *owner = nullptr;
  if (owner == &root)
    bodyObject = nullptr;
}
static void bodyDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) == bodyObject)
    bodyObject = nullptr;
}
void close() {
  bodyObject = nullptr;
  if (root) {
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    host.closeRoot(&root);
  }
}
void closeTraceResult() {
  if (traceRoot)
    host.closeRoot(&traceRoot);
}
bool isOpen() { return root != nullptr; }
bool traceResultOpen() { return traceRoot != nullptr; }
lv_obj_t *scrollBody() { return bodyObject; }
void configure(const Host &value) {
  close();
  closeTraceResult();
  host = value;
}
static void backdrop(lv_event_t *event) {
  if (owns(event, root))
    close();
}
static void traceBackdrop(lv_event_t *event) {
  if (owns(event, traceRoot))
    closeTraceResult();
}
static void copyEvent(lv_event_t *event) {
  if (owns(event, root) || owns(event, traceRoot))
    host.copyLabel(event);
}
static void clampEvent(lv_event_t *event) {
  if (owns(event, root))
    host.clampScroll(event);
}
static void traceEvent(lv_event_t *event) {
  if (!owns(event, root) || !host.activeConversation(selected))
    return;
  close();
  host.trace();
}
static void replayEvent(lv_event_t *event) {
  if (!owns(event, root))
    return;
  const Message message = selected;
  if (host.prepareRoute(message) < 2)
    return;
  close();
  host.replay();
}
// snprintf reports the untruncated length. Clamp after every append so a long
// route can never turn the next remaining-capacity calculation into an underflow.
static void append(char *output, size_t size, int &used, const char *format, ...) {
  if (used < 0 || static_cast<size_t>(used) >= size)
    return;
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(output + used, size - used, format, args);
  va_end(args);
  if (length > 0)
    used += static_cast<size_t>(length) < size - used ? length : static_cast<int>(size - used - 1);
}
void showTraceResult(const char *title, const char *body) {
  closeTraceResult();
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  traceRoot = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(traceRoot, rootDeleted, LV_EVENT_DELETE, &traceRoot);
  lv_obj_remove_style_all(traceRoot);
  lv_obj_set_size(traceRoot, sw, sh - host.statusHeight());
  lv_obj_set_pos(traceRoot, 0, host.statusHeight());
  lv_obj_set_style_bg_color(traceRoot, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(traceRoot, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(traceRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(traceRoot, traceBackdrop, LV_EVENT_CLICKED, nullptr);

#if CAP_LARGE_SCREEN
  const int card_w = PCW(220);
  const int card_h = PSC(236);
#else
  const int card_w = 220;
  const int card_h = 236;
#endif
  lv_obj_t *card = lv_obj_create(traceRoot);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  addCloseXBadge(card, traceBackdrop);

  lv_obj_t *ttl = lv_label_create(card);
  lv_label_set_text(ttl, title);
  lv_obj_set_style_text_color(ttl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(ttl, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ttl, card_w - 20 - 32);
  lv_obj_set_pos(ttl, 0, 4);

  lv_obj_t *lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, card_w - 20);
  lv_label_set_text(lbl, body);
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(lbl, 0, PSC(32));
  lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl, copyEvent, LV_EVENT_LONG_PRESSED, const_cast<char *>("info"));
}

static uint32_t nodeSigColorHex(const char *s) {
  if (!s || !s[0])
    return 0xFFFFFFu;
  lv_color_t bg, nc;
  usernameBubbleColors(s, &bg, &nc);
  return lv_color_to32(nc) & 0xFFFFFFu;
}

void show(int msg_idx) {
  MessageTypes::UIMessage m;
  if (!host.readMessage || !host.readMessage(msg_idx, m))
    return;
  close();
  selected = m;

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, &root);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - host.statusHeight());
  lv_obj_set_pos(root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, backdrop, LV_EVENT_CLICKED, nullptr);

  // DM messages get a "Trace route" button (live multi-hop SNR trace to the
  // peer). Any message whose repeater path we can place on the map also gets a
  // "Replay" button. Build the route now so we know whether to offer it.
  const bool show_trace = !m.channel;
  const int route_pts = host.prepareRoute(m);
  const bool show_route = (route_pts >= 2);
#if defined(TLORA_PAGER)
  // Reduce wrapping across the wide, short Pager. The independently scrollable
  // body handles routes and ACK details that exceed the available height.
  const int card_w = sw - 28;
  int card_h = sh - host.statusHeight() - 8;
#elif CAP_LARGE_SCREEN
  const int card_w = PCW(220);
  int card_h = (show_trace || show_route) ? PSC(290) : PSC(250);
#else
  const int card_w = 220;
  int card_h = (show_trace || show_route) ? 290 : 250;
#endif
  // Never taller than the screen; the body inside scrolls if it doesn't fit
  // (matters on the short 320x240 T-Deck in landscape).
  const int avail_h = sh - host.statusHeight() - 8;
  if (card_h > avail_h)
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
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, backdrop);

  // Build a single multi-line label — keeps layout dead simple, and the
  // user can long-press it (clipboardSet) to copy the whole metadata
  // dump if they ever need it. Body text:
  //   Message info
  //   ────────────
  //   Time   2026-05-27 22:34:01
  //   From   <sender>            (only when incoming)
  //   Path   flood, 2 hops       (or "direct (routed)" or "—")
  //   SNR    -3.5 dB             (or "—")
  //   RSSI   -101 dBm            (or "—")
  //   Status sent / delivered / failed   (only when outgoing DM)
  char body[1400]; // headroom for the FULL per-hop route list + recolor markers (#RRGGBB ... #)
  int blen = 0;
  // Title is rendered as a separate label below — don't repeat it in body.
  // Time
  char tbuf[40];
  formatFullTimestamp(m.ts, tbuf, sizeof(tbuf));
  append(body, sizeof body, blen, TR("Time   %s"), tbuf);
  // From (incoming only — outgoing is obviously us)
  if (!m.outgoing && m.sender[0]) {
    append(body, sizeof body, blen, "\nFrom   %s", m.sender);
  }
  // Direction / channel/dm
  append(body, sizeof body, blen, "\nDir    %s%s", m.outgoing ? "outgoing" : "incoming",
         m.channel ? " (channel)" : " (DM)");
  // RX metadata: only meaningful for incoming messages received post-v4.
  const bool has_rx = (m.meta_flags & MessageTypes::MSG_META_HAS_RX) != 0;
  if (has_rx) {
    const bool is_flood = (m.meta_flags & MessageTypes::MSG_META_IS_FLOOD) != 0;
    if (is_flood) {
      append(body, sizeof body, blen, "\nPath   flood, %u hops, %u-byte hash", (unsigned)(m.path_len & 0x3F),
             (unsigned)((m.path_len >> 6) + 1));
    } else {
      append(body, sizeof body, blen, "\nPath   direct (routed)");
    }
    append(body, sizeof body, blen, "\nSNR    %.2f dB", (double)m.snr_q4 / 4.0);
    append(body, sizeof body, blen, "\nRSSI   %d dBm", (int)m.rssi);
    if (m.meta_flags & MessageTypes::MSG_META_HAS_SCOPE) {
      // The raw code is an HMAC of this packet under the sender's region key, so
      // it is different on every message and means nothing on its own — showing
      // the bare hex read as "the region", and people reasonably asked why it kept
      // changing (#259). Report what was actually determined: whether it verified
      // against our own region key. The hex stays, in parentheses, for anyone
      // cross-checking a capture.
      // #271: when the code verified against one of the registered regions we can
      // name it outright. Fall back to the older my-region / another-region
      // answer when nothing matched, which is still all we can honestly say.
      char region[40];
      const uint8_t rslot = MessageTypes::metaScopeSlot(m.meta_flags);
      const char *rname = (rslot && rslot != ambiguousRegionSlot) ? host.regionName(rslot) : nullptr;
      if (rslot == ambiguousRegionSlot) {
        // Several registered regions produced this same 16-bit code. Naming any
        // one of them would be a guess, and a confident wrong region reads worse
        // than an honest "cannot tell". The tag is only 16 bits, so with N keys
        // live a collision runs at about N/65534 per packet.
        snprintf(region, sizeof region, "%s", TR("ambiguous (several regions matched)"));
      } else if (rname) {
        snprintf(region, sizeof region, "%s", rname);
      } else if (m.meta_flags & MessageTypes::MSG_META_SCOPE_HOME) {
        char home[24] = {0};
        touchPrefsGetRegionScope(home, sizeof home);
        snprintf(region, sizeof region, "%s", home[0] ? home : TR("my region"));
      } else {
        snprintf(region, sizeof region, "%s", TR("another region"));
      }
      // Key is "Scope" with no trailing space: TR() strips icon prefixes, NOT
      // trailing whitespace, so "Scope " would never match its .lang row.
      append(body, sizeof body, blen, "\n%s  %s (%04X)", TR("Scope"), region, (unsigned)m.in_scope);
    }
    // Full inbound route — the repeaters this flood traversed. Resolve each hop's
    // hash to its repeater name when that contact is known. EVERY hop is listed
    // (the popup body scrolls); a hard buffer guard is the only bound. When a hop
    // resolves to a name we show just the name — appending the raw hash hex made
    // long names wrap so the trailing hash ran into the next hop's number
    // ("…Toenis 31" + "3." read as "313."). The hash is only shown as a fallback
    // when the name is unknown. The name lookup is read-only so it's safe mid-RX.
    if (is_flood && m.in_path_n > 0) {
      const uint8_t hsz = (uint8_t)((m.path_len >> 6) + 1);
      const uint8_t cnt = (uint8_t)(m.path_len & 0x3F);
      append(body, sizeof body, blen, "\nRoute");
      int off = 0;
      for (uint8_t h = 0; h < cnt && off + (int)hsz <= m.in_path_n; ++h, off += hsz) {
        if (blen >= (int)sizeof(body) - 72)
          break; // hard guard: never overflow body[]
        // Always lead with the path-hash prefix, then the name when we know it.
        // The hash is what the node is actually keyed by on the wire, so it is
        // the part that stays true when a repeater is named misleadingly or two
        // of them share a name -- which is exactly when you are reading a route
        // (#348). Showing only the resolved name threw that away.
        char hashstr[9] = {0};
        for (uint8_t b = 0; b < hsz && b < 4; ++b)
          snprintf(hashstr + b * 2, sizeof(hashstr) - b * 2, "%02X", m.in_path[off + b]);
        char nm[33]; // ContactInfo.name is 32B; hold the full name (emoji eat 4B each)
        if (host.hopName(&m.in_path[off], hsz, nm, sizeof(nm))) {
          append(body, sizeof body, blen, "\n %u. #%06X %s %s#", (unsigned)(h + 1),
                 (unsigned)nodeSigColorHex(nm), hashstr, nm);
        } else {
          append(body, sizeof body, blen, "\n %u. #%06X %s#", (unsigned)(h + 1),
                 (unsigned)nodeSigColorHex(hashstr), hashstr);
        }
      }
    }
  } else if (!m.outgoing) {
    append(body, sizeof body, blen,
           "\nPath   \xe2\x80\x94"
           "\nSNR    \xe2\x80\x94"
           "\nRSSI   \xe2\x80\x94");
  }
  // Delivery state (outgoing DMs only)
  if (m.outgoing && !m.channel) {
    const char *ds = "—";
    switch (m.deliv_state) {
    case MessageTypes::DELIV_NONE:
      ds = "queued";
      break;
    case MessageTypes::DELIV_SENT:
      ds = "sent";
      break;
    case MessageTypes::DELIV_DELIVERED:
      ds = "delivered";
      break;
    case MessageTypes::DELIV_FAILED:
      ds = "failed";
      break;
    }
    append(body, sizeof body, blen, "\nStatus %s", ds);
  }
  // "Repeats heard": echoes of our sent flood re-broadcast by nearby repeaters,
  // counted live this session. Shown for any tracked outgoing flood.
  if (m.outgoing && m.sent_fp) {
    append(body, sizeof body, blen, "\nRepeats heard: %u", (unsigned)host.repeats(m.sent_fp));
    // List the repeaters we caught echoing it (name where known, else the hash).
    // Bounded (<= ECHO_MAX_HOPS) + hard buffer guard + read-only lookup -> no risk.
    uint8_t rhc = host.repeatCount(m.sent_fp);
    for (uint8_t r = 0; r < rhc; r++) {
      if (blen >= (int)sizeof(body) - 96)
        break; // escaped name (<=64B) + markers must fit whole
      uint8_t hh[4];
      uint8_t hsz = host.repeatHop(m.sent_fp, r, hh, sizeof(hh));
      if (hsz == 0)
        continue;
      char hashstr[9] = {0};
      for (uint8_t b = 0; b < hsz && b < 4; ++b)
        snprintf(hashstr + b * 2, sizeof(hashstr) - b * 2, "%02X", hh[b]);
      char nm[33]; // ContactInfo.name is 32B; hold the full name (emoji eat 4B each)
      if (host.hopName(hh, hsz, nm, sizeof(nm))) {
        // #223 class: this name sits inside a '#RRGGBB …#' run — a literal '#' or a
        // broken UTF-8 tail in it desyncs every colour block after this line.
        char nm_c[33], nm_esc[66];
        host.sanitize(nullptr, nm_c, sizeof nm_c, nm);
        recolorEscape(nm_esc, sizeof nm_esc, nm_c);
        append(body, sizeof body, blen, "\n  #%06X %s#", (unsigned)nodeSigColorHex(nm),
               nm_esc); // name resolved -> no redundant hash
      } else
        append(body, sizeof body, blen, "\n  #%06X %s#", (unsigned)nodeSigColorHex(hashstr), hashstr);
    }
  }
  if (blen >= (int)sizeof(body))
    blen = sizeof(body) - 1;
  body[blen] = '\0';

  // Title row at top-left, sized to clear the close-X (top-right 32 px). Pinned.
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR("Message info"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 20 - 32);
  lv_obj_set_pos(title, 0, 4);

  // Action row pinned just below the title (ABOVE the route info), then the
  // scrollable body below it. "Replay" (animate this message's repeater path on
  // the map) and/or "Trace route" (live multi-hop SNR trace, DM only) — shown
  // side-by-side when both apply, else full width.
  const int content_h = card_h - 20; // card has pad_all = 10
  const bool has_btns = (show_route || show_trace);
  const int btn_h = PSC(32);
  const int btn_row_y = PSC(30); // just below the title
  const int body_top = has_btns ? (btn_row_y + btn_h + PSC(8)) : PSC(30);

  if (has_btns) {
    if (show_route && show_trace) {
      const int bw = (card_w - 20 - 6) / 2;
      lv_obj_t *rb = lv_btn_create(card);
      lv_obj_set_size(rb, bw, btn_h);
      lv_obj_set_pos(rb, 0, btn_row_y);
      styleButton(rb);
      lv_obj_add_event_cb(rb, replayEvent, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *rl = lv_label_create(rb);
      lv_label_set_text_fmt(rl, LV_SYMBOL_GPS " %s", TR("Replay"));
      lv_obj_set_style_text_font(rl, &font12(), LV_PART_MAIN);
      lv_obj_center(rl);

      lv_obj_t *tb = lv_btn_create(card);
      lv_obj_set_size(tb, bw, btn_h);
      lv_obj_set_pos(tb, bw + 6, btn_row_y);
      styleButton(tb);
      lv_obj_add_event_cb(tb, traceEvent, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *tl = lv_label_create(tb);
      lv_label_set_text(tl, TR("Trace"));
      lv_obj_set_style_text_font(tl, &font12(), LV_PART_MAIN);
      lv_obj_center(tl);
    } else {
      lv_obj_t *b = lv_btn_create(card);
      lv_obj_set_size(b, card_w - 20, btn_h);
      lv_obj_set_pos(b, 0, btn_row_y);
      styleButton(b);
      lv_obj_add_event_cb(b, show_route ? replayEvent : traceEvent, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *l = lv_label_create(b);
      lv_label_set_text_fmt(l, LV_SYMBOL_GPS "  %s",
                            show_route ? TR("Replay route on map") : TR("Trace route"));
      lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
      lv_obj_center(l);
    }
  }

  // Scrollable body below the action row. recolor is on so each repeater's
  // identifier is tinted with its chat-bubble colour (markers built into `body`).
  lv_obj_t *bodywrap = lv_obj_create(card);
  bodyObject = bodywrap;
  lv_obj_add_event_cb(bodyObject, bodyDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(bodywrap);
  lv_obj_set_size(bodywrap, card_w - 20, content_h - body_top);
  lv_obj_set_pos(bodywrap, 0, body_top);
  lv_obj_set_style_bg_opa(bodywrap, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_scroll_dir(bodywrap, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(bodywrap,
#if defined(TLORA_PAGER)
                            LV_SCROLLBAR_MODE_ON
#else
                            LV_SCROLLBAR_MODE_AUTO
#endif
  );
  lv_obj_add_event_cb(bodywrap, clampEvent, LV_EVENT_SCROLL_END, nullptr);
  // Thin visible scrollbar (remove_style_all wiped the theme's).
  lv_obj_set_style_width(bodywrap, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(bodywrap, lv_color_hex(0x6FA8DA), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(bodywrap, LV_OPA_70, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(bodywrap, 2, LV_PART_SCROLLBAR);

  lv_obj_t *lbl = lv_label_create(bodywrap);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_recolor(lbl, true);
  lv_obj_set_width(lbl, card_w - 20 - 8); // leave room for the scrollbar
  lv_label_set_text(lbl, body);
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(lbl, 0, 0);
  // Long-press the body to copy the full dump (a drag scrolls, a hold copies;
  // recolour markers are stripped on copy).
  lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl, copyEvent, LV_EVENT_LONG_PRESSED, const_cast<char *>("info"));

  // Bottom-right Close button removed — the X badge at the top-right is
  // the standard dismiss affordance now (same as every other popup).
}

} // namespace messageInfo
} // namespace screens
} // namespace ui
