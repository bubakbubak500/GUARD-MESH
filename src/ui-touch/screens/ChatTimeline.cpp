// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatTimeline.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../ChannelSenderSplit.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ChatText.h"
#include "../widgets/Styles.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ui {
namespace screens {
namespace timeline {
using namespace ui::theme;
using namespace ui::widgets;
using std::max;
using std::min;
static Host host{};
static Metrics work{};
static uint32_t rowStyleEpoch = 1;
static LvChatPanel *s_chat_virt_render_async_panel = nullptr;
static bool s_chat_virt_render_async_busy = false;
static uint8_t s_chat_detail_async_mask = 0;
static bool s_chat_detail_async_queued = false;
static void chatVirtRenderAsyncCb(void *);
static void refreshChatDetailAsyncCb(void *);
static void watchMessages(ChatPanel &);

static FocusRequest focusRequest{-1, 0};
#ifndef TRACE_MESSAGE_SCROLL_ACTIVITY
#define TRACE_MESSAGE_SCROLL_ACTIVITY 0
#endif
#if TRACE_MESSAGE_SCROLL_ACTIVITY
static void trace(const char *format, ...) {
  if (!host.trace)
    return;
  char text[320];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  host.trace(text);
}
#define CHAT_SCROLL_TRACE_PRINTF(...) trace(__VA_ARGS__)
#define CHAT_SCROLL_TRACE_DO(stmt)                                                                           \
  do {                                                                                                       \
    stmt;                                                                                                    \
  } while (0)
#else
#define CHAT_SCROLL_TRACE_PRINTF(...)                                                                        \
  do {                                                                                                       \
  } while (0)
#define CHAT_SCROLL_TRACE_DO(stmt)                                                                           \
  do {                                                                                                       \
  } while (0)
#endif
static uint16_t s_unread_at_open = 0;
static LvChatPanel *s_last_tap_panel = nullptr;
static uint32_t s_last_tap_ms = 0;
static lv_point_t s_last_tap_point{};
static bool chatTapToLatest(LvChatPanel *p) {
  if (!p || !p->detail_open || !p->msgs) return false;
  lv_indev_t *indev = lv_indev_get_act();
  lv_point_t point{};
  if (indev) lv_indev_get_point(indev, &point);
  const uint32_t now = ui::platform::milliseconds();
  const bool second = s_last_tap_panel == p && uint32_t(now - s_last_tap_ms) <= 350 &&
      abs(point.x - s_last_tap_point.x) <= 40 && abs(point.y - s_last_tap_point.y) <= 40;
  s_last_tap_panel = second ? nullptr : p;
  s_last_tap_ms = now;
  s_last_tap_point = point;
  if (second) chatVirtJumpToLatest(p);
  return second;
}
static void chatBackgroundTap(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    chatTapToLatest(static_cast<LvChatPanel *>(lv_event_get_user_data(event)));
}

static bool s_chat_just_opened = false;

static int s_chat_jump_msg_idx = -1; // ring-slot index to scroll to on open (tapped @mention), or -1

static void chatDetailShowPlaceholder(LvChatPanel &p, const char *msg) {
  lv_obj_t *lbl = lv_label_create(p.msgs);
  useChainedFont(lbl);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, 200);
  lv_label_set_text(lbl, TR(msg));
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(lbl);
}
// ---- Virtualized chat bubbles ------------------------------------------------
// Message data lives in the _messages._ui_msgs ring (up to MAX_UI_MESSAGES). Only a small
// window of LVGL bubble widgets is materialised for the visible scroll range.
static constexpr int kChatVirtOverscanPx = 120;
static constexpr lv_coord_t kChatBubblePadH = 8;
static constexpr lv_coord_t kChatBubblePadV = 5;
static constexpr lv_coord_t kChatSideGutter = 2;
static constexpr lv_coord_t kChatRowGap = 4;
static constexpr lv_coord_t kChatCompactRowGap = 2;
static constexpr lv_coord_t kChatDividerH = 16;

static lv_coord_t chatMeasureBubbleHeight(const MessageTypes::UIMessage &m, bool channel_mode,
                                          bool thread_is_room, lv_coord_t bubble_max_w);
static lv_coord_t chatMeasureMessageRowHeight(const MessageTypes::UIMessage &m, LvChatPanel *p,
                                              int logical_i);
static lv_coord_t chatVirtCreateMessageRow(LvChatPanel *p, int logical_i, int ring_idx, lv_coord_t vp_y,
                                           lv_coord_t *out_jump_y);
static lv_coord_t chatVirtMsgViewportY(int logical_i, int32_t virt_top);
static void chatBuildBubbleMeta(const MessageTypes::UIMessage &m, bool channel_mode, char *out,
                                size_t out_sz, uint32_t *fg);

void chatVirtRefreshScrollArea(LvChatPanel *p);

struct ChatBubbleDisplay {
  const char *show_sender = nullptr;
  const char *show_text = nullptr;
  char retro_sender[MessageTypes::MAX_SENDER_NAME + 1];
  char san_sender[MessageTypes::MAX_SENDER_NAME + 8];
  char san_text[MessageTypes::MAX_MSG_TEXT + 8];
};

struct ChatVirtLayout {
  LvChatPanel *panel = nullptr;
  int n = 0;
  int divider_i = -1;
  int32_t divider_y = -1;
  int last_i0 = -1;
  int last_i1 = -1;
  bool thread_is_room = false;
  bool compact_chat = false;
  char compact_thread_name[MessageTypes::MAX_THREAD_NAME + 1] = "";
  lv_coord_t content_w = 0;
  lv_coord_t bubble_max_w = 0;
  int *msg_idx = nullptr;
  int32_t *offsets = nullptr;   // virt Y per message; offsets[n] = virt total height
  int32_t *day_sep_y = nullptr; // virt Y of day label before message i, or -1
  int offset_capacity = 0;
  // Ring slots of the first/last laid-out message. Content-generation guard: the
  // ring evicting or rotating (n unchanged at capacity) and thread switches both
  // move these, so refreshChatDetail can tell "same count, different content"
  // apart from "nothing changed" (two threads can never share a ring slot).
  int first_ring = -1;
  int last_ring = -1;
  int32_t virt_total_h = 0;
  lv_coord_t lv_total_h = 0;
  lv_obj_t *spacer = nullptr;
  lv_coord_t spacer_w = 0, spacer_h = 0;
  lv_obj_t *divider = nullptr;
  bool pending_scroll = false;
  bool pending_scroll_bottom = false;
  lv_coord_t pending_scroll_y = 0;
  // When LVGL scroll coords are compressed, finger drag still moves content 1:1 in
  // layout (virt) pixels; scroll_y only tracks scrollbar thumb position.
  int32_t scroll_virt_top = 0;
  lv_coord_t scroll_lv_anchor = 0;
  bool scroll_virt_valid = false;
};

static ChatVirtLayout s_chat_virt;
static int *s_chat_msg_idx = nullptr;
static int s_chat_msg_idx_cap = 0;
struct HeightEntry { uint32_t sequence = 0, signature = 0; lv_coord_t height = 0; };
static HeightEntry* heightCache = nullptr;
static uint32_t hashBytes(uint32_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (size--) hash = (hash ^ *bytes++) * 16777619u;
  return hash;
}
static uint32_t contentSignature(const MessageTypes::UIMessage& m) {
  uint32_t hash = hashBytes(2166136261u, m.text, strnlen(m.text, sizeof m.text));
  hash = hashBytes(hash, m.sender, strnlen(m.sender, sizeof m.sender));
  hash = hashBytes(hash, &m.ts, sizeof m.ts);
  hash = hashBytes(hash, &m.outgoing, sizeof m.outgoing);
  hash = hashBytes(hash, &s_chat_virt.content_w, sizeof s_chat_virt.content_w);
  hash = hashBytes(hash, &s_chat_virt.thread_is_room, sizeof s_chat_virt.thread_is_room);
  hash = hashBytes(hash, &s_chat_virt.compact_chat, sizeof s_chat_virt.compact_chat);
  const auto* font = chatMessageFont();
  hash = hashBytes(hash, &font, sizeof font);
  hash = hashBytes(hash, &rowStyleEpoch, sizeof rowStyleEpoch);
  char meta[48];
  chatBuildBubbleMeta(m, s_chat_virt.panel && s_chat_virt.panel->channel_mode, meta, sizeof meta, nullptr);
  const bool hasMeta = meta[0];
  hash = hashBytes(hash, &hasMeta, sizeof hasMeta);
  if (s_chat_virt.compact_chat) {
    hash = hashBytes(hash, s_chat_virt.compact_thread_name, strlen(s_chat_virt.compact_thread_name));
    hash = hashBytes(hash, &m.deliv_state, sizeof m.deliv_state);
    hash = hashBytes(hash, &m.path_len, sizeof m.path_len);
    const uint8_t repeats = m.sent_fp && host.repeats ? host.repeats(m.sent_fp) : 0;
    hash = hashBytes(hash, &repeats, sizeof repeats);
  }
  if (s_chat_virt.panel) hash = hashBytes(hash, &s_chat_virt.panel->channel_mode, sizeof(bool));
  return hash;
}
static lv_timer_t *s_chat_virt_render_timer = nullptr;
static LvChatPanel *s_chat_virt_render_panel = nullptr;

// Layout offsets are int32; LVGL scroll coords are int16 (~8191 max). When virt
// height exceeds the cap, scroll Y is compressed for the spacer/scrollbar but bubble
// viewport positions use virt-space 1:1 (offsets[i] - virt_top). Finger drags also
// advance virt_top 1:1 (see chatVirtRemap1To1Scroll) so scroll speed matches touch.
static constexpr int32_t kChatVirtLvScrollMax = 7500;

static bool chatVirtCompressCoords() { return s_chat_virt.virt_total_h > kChatVirtLvScrollMax; }

lv_coord_t chatVirtMsgsViewH(LvChatPanel *p) {
  if (!p || !p->msgs)
    return 0;
  const lv_coord_t h = lv_obj_get_height(p->msgs);
  const lv_coord_t pad_t = lv_obj_get_style_pad_top(p->msgs, LV_PART_MAIN);
  const lv_coord_t pad_b = lv_obj_get_style_pad_bottom(p->msgs, LV_PART_MAIN);
  const lv_coord_t inner = h - pad_t - pad_b;
  return inner > 0 ? inner : h;
}

// Max scroll Y from layout metadata — reliable before LVGL has recomputed
// scroll_bottom after the virt spacer is first sized (open-to-bottom landed at 0).
lv_coord_t chatVirtMaxScrollY(LvChatPanel *p) {
  if (!p || !p->msgs || s_chat_virt.lv_total_h <= 0)
    return 0;
  chatVirtRefreshScrollArea(p);
  const lv_coord_t view_h = chatVirtMsgsViewH(p);
  lv_coord_t max_y = s_chat_virt.lv_total_h - view_h;
  return max_y > 0 ? max_y : 0;
}

bool chatVirtAwayFromBottom(LvChatPanel *p) {
  if (!p || !p->msgs)
    return false;
  if (lv_obj_get_scroll_bottom(p->msgs) > 30)
    return true;
  const lv_coord_t max_y = chatVirtMaxScrollY(p);
  return max_y > 30 && lv_obj_get_scroll_y(p->msgs) < max_y - 30;
}

static lv_coord_t chatVirtLastBubbleHeight(LvChatPanel *p, int n) {
  if (!p || n <= 0 || !s_chat_msg_idx || !host.ready())
    return 40;
  MessageTypes::UIMessage m;
  if (!host.messageAt(s_chat_msg_idx[n - 1], m))
    return 40;
  return chatMeasureMessageRowHeight(m, p, n - 1);
}

static void chatVirtUpdateLvScale(LvChatPanel *p, int n, int32_t virt_total) {
  s_chat_virt.virt_total_h = virt_total;
  if (!chatVirtCompressCoords()) {
    s_chat_virt.lv_total_h = static_cast<lv_coord_t>(virt_total);
    return;
  }
  (void)n;
  // Make LVGL's max scroll map back to the real layout bottom. Bubble viewport
  // positions are offsets[i] - lvToVirt(scroll_y), so the bottom state must be
  // virt_total - view_h rather than a special bottom-pinned layout.
  const lv_coord_t view_h = chatVirtMsgsViewH(p);
  const int32_t max_virt_top = (virt_total > view_h) ? (virt_total - static_cast<int32_t>(view_h)) : 0;
  lv_coord_t need =
      static_cast<lv_coord_t>((static_cast<int64_t>(max_virt_top) * kChatVirtLvScrollMax) / virt_total) +
      view_h;
  if (need > static_cast<lv_coord_t>(LV_COORD_MAX - 8))
    need = static_cast<lv_coord_t>(LV_COORD_MAX - 8);
  s_chat_virt.lv_total_h = need;
}

lv_coord_t chatVirtVirtToLv(int32_t virt_y) {
  if (s_chat_virt.virt_total_h <= 0)
    return 0;
  if (!chatVirtCompressCoords())
    return static_cast<lv_coord_t>(virt_y);
  return static_cast<lv_coord_t>((static_cast<int64_t>(virt_y) * kChatVirtLvScrollMax) /
                                 s_chat_virt.virt_total_h);
}

static int32_t chatVirtLvToVirt(lv_coord_t lv_y) {
  if (s_chat_virt.virt_total_h <= 0)
    return 0;
  if (!chatVirtCompressCoords())
    return static_cast<int32_t>(lv_y);
  return static_cast<int32_t>((static_cast<int64_t>(lv_y) * s_chat_virt.virt_total_h) / kChatVirtLvScrollMax);
}

static int32_t chatVirtLvViewToVirt(lv_coord_t lv_view_h) {
  if (s_chat_virt.virt_total_h <= 0)
    return 0;
  if (!chatVirtCompressCoords())
    return static_cast<int32_t>(lv_view_h);
  return static_cast<int32_t>((static_cast<int64_t>(lv_view_h) * s_chat_virt.virt_total_h) /
                              kChatVirtLvScrollMax);
}

int32_t chatVirtMaxVirtTop(LvChatPanel *p) {
  if (s_chat_virt.virt_total_h <= 0)
    return 0;
  const lv_coord_t view_h = p ? chatVirtMsgsViewH(p) : 0;
  const int32_t vh = static_cast<int32_t>(view_h);
  return (s_chat_virt.virt_total_h > vh) ? (s_chat_virt.virt_total_h - vh) : 0;
}

static int32_t chatVirtEffectiveVirtTop(LvChatPanel *p) {
  if (!p || !p->msgs)
    return 0;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(p->msgs);
  if (!chatVirtCompressCoords())
    return static_cast<int32_t>(scroll_y);
  if (s_chat_virt.scroll_virt_valid)
    return s_chat_virt.scroll_virt_top;
  return chatVirtLvToVirt(scroll_y);
}

static void chatVirtSyncScrollState(LvChatPanel *p, lv_coord_t lv_y, int32_t virt_override = -1) {
  if (!p || !chatVirtCompressCoords()) {
    s_chat_virt.scroll_virt_valid = false;
    return;
  }
  s_chat_virt.scroll_virt_valid = true;
  s_chat_virt.scroll_virt_top = (virt_override >= 0) ? virt_override : chatVirtLvToVirt(lv_y);
  const int32_t max_top = chatVirtMaxVirtTop(p);
  if (s_chat_virt.scroll_virt_top < 0)
    s_chat_virt.scroll_virt_top = 0;
  else if (s_chat_virt.scroll_virt_top > max_top)
    s_chat_virt.scroll_virt_top = max_top;
  s_chat_virt.scroll_lv_anchor = chatVirtVirtToLv(s_chat_virt.scroll_virt_top);
}

// Remap LVGL's 1:1 finger→scroll_y delta into a 1:1 finger→virt_top delta when coords
// are compressed (otherwise a small drag jumps through a huge history chunk).
void chatVirtRemap1To1Scroll(LvChatPanel *p) {
  if (!p || !p->msgs || !chatVirtCompressCoords())
    return;
  const lv_coord_t lv_y = lv_obj_get_scroll_y(p->msgs);
  if (!s_chat_virt.scroll_virt_valid) {
    chatVirtSyncScrollState(p, lv_y);
    return;
  }
  const lv_coord_t delta = lv_y - s_chat_virt.scroll_lv_anchor;
  if (delta == 0)
    return;
  s_chat_virt.scroll_virt_top += static_cast<int32_t>(delta);
  const int32_t max_top = chatVirtMaxVirtTop(p);
  if (s_chat_virt.scroll_virt_top < 0)
    s_chat_virt.scroll_virt_top = 0;
  else if (s_chat_virt.scroll_virt_top > max_top)
    s_chat_virt.scroll_virt_top = max_top;
  // An animated scroll (the keypad page scroll in navScrollBy) delivers this
  // event from inside LVGL's animation step, and lv_obj_scroll_to_y() deletes
  // that running animation. LVGL 8.4 keeps using it after we return: when the
  // step was also its last, it frees the animation a second time, releasing
  // whatever reused the memory in between. That is the #428/#475 panic (a jump
  // to 0x00020000 out of anim_timer). The rows are floating and follow virt_top,
  // so just track the animation; chatVirtOnScrollEnd re-anchors once LVGL has
  // unlinked it. The early snap also stopped every such scroll after one frame.
  if (lv_anim_get(p->msgs, nullptr)) {
    s_chat_virt.scroll_lv_anchor = lv_y;
    return;
  }
  const lv_coord_t corrected = chatVirtVirtToLv(s_chat_virt.scroll_virt_top);
  s_chat_virt.scroll_lv_anchor = corrected;
  if (corrected != lv_y)
    lv_obj_scroll_to_y(p->msgs, corrected, LV_ANIM_OFF);
}

static bool chatVirtNearStoreBottom(LvChatPanel *p, lv_coord_t scroll_y) {
  (void)scroll_y;
  if (!p || !p->msgs || s_chat_virt.n <= 0)
    return false;
  return lv_obj_get_scroll_bottom(p->msgs) <= 24;
}

// Message index whose virt extent contains virt_top (viewport top in layout space).
static int chatVirtFindMsgAtVirtTop(int32_t virt_top) {
  if (!s_chat_virt.offsets || s_chat_virt.n <= 0)
    return 0;
  if (virt_top <= 0)
    return 0;
  const int32_t *first = s_chat_virt.offsets;
  const int32_t *last = first + s_chat_virt.n;
  return static_cast<int>(std::upper_bound(first, last, virt_top) - first) - 1;
}

static lv_coord_t chatVirtMeasuredHeightAt(LvChatPanel *p, int logical_i) {
  if (!p || !host.ready() || !s_chat_msg_idx || logical_i < 0 || logical_i >= s_chat_virt.n)
    return 40;
  MessageTypes::UIMessage m;
  if (!host.messageAt(s_chat_msg_idx[logical_i], m))
    return 40;
  return chatMeasureMessageRowHeight(m, p, logical_i);
}

#if TRACE_MESSAGE_SCROLL_ACTIVITY
void chatVirtLogTopAnchor(const char *tag, LvChatPanel *p, lv_coord_t scroll_y, int touch_x, int touch_y) {
  if (!tag || !p || s_chat_virt.n <= 0 || !s_chat_virt.offsets)
    return;
  const int32_t virt_top = chatVirtLvToVirt(scroll_y);
  const int msg_i = chatVirtFindMsgAtVirtTop(virt_top);
  const int virt_off = (int)(virt_top - s_chat_virt.offsets[msg_i]);
  const int vp_y = (int)chatVirtMsgViewportY(msg_i, virt_top);
  if (touch_x >= 0)
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] %s touch=(%d,%d) scroll_y=%d top=Msg#%d @Y%+d virt_off=%d\n", tag,
                             touch_x, touch_y, (int)scroll_y, msg_i + 1, vp_y, virt_off);
  else
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] %s scroll_y=%d top=Msg#%d @Y%+d virt_off=%d\n", tag, (int)scroll_y,
                             msg_i + 1, vp_y, virt_off);
}
#endif

// Only materialise new bubbles when the visible range extends past what we already have.
static bool chatVirtNeedReflow(int new_i0, int new_i1) {
  if (s_chat_virt.last_i0 < 0 || s_chat_virt.last_i1 < 0)
    return true;
  return new_i0 < s_chat_virt.last_i0 || new_i1 > s_chat_virt.last_i1;
}

void chatVirtCancelRenderTimer() {
  s_chat_virt_render_panel = nullptr;
  lv_async_call_cancel(chatVirtRenderAsyncCb, nullptr);
  s_chat_virt_render_async_panel = nullptr;
  s_chat_virt_render_async_busy = false;
  // Keep one timer allocation for the lifetime of the UI.  This path is hit
  // repeatedly while a fast scroll crosses virtual-window boundaries, and
  // pausing cancels the pending work without churning the timer list. (The
  // #251 dumps that prompted this looked like a bad timer callback; the same
  // signature in #428 was a freed animation, see chatVirtRemap1To1Scroll.)
  if (s_chat_virt_render_timer)
    lv_timer_pause(s_chat_virt_render_timer);
}

void chatVirtResetInputForMsgs(LvChatPanel *p) {
  lv_indev_t *act = lv_indev_get_act();
  if (act)
    lv_indev_wait_release(act);
  if (p && p->msgs)
    lv_indev_reset(nullptr, p->msgs);
}

static void chatVirtFreeOffsets() {
  if (s_chat_virt.offsets) {
    ui::platform::release(s_chat_virt.offsets);
    s_chat_virt.offsets = nullptr;
  }
  if (s_chat_virt.day_sep_y) {
    ui::platform::release(s_chat_virt.day_sep_y);
    s_chat_virt.day_sep_y = nullptr;
  }
  s_chat_virt.offset_capacity = 0;
}
static bool ensureOffsetCapacity(int n) {
  if (s_chat_virt.offsets && s_chat_virt.day_sep_y && s_chat_virt.offset_capacity >= n) return true;
  int capacity = max(128, s_chat_virt.offset_capacity);
  while (capacity < n) capacity *= 2;
  capacity = min(capacity, max(n, s_chat_msg_idx_cap));
  auto* offsets = static_cast<int32_t*>(ui::platform::allocate(sizeof(int32_t) * (capacity + 1), true));
  auto* separators = static_cast<int32_t*>(ui::platform::allocate(sizeof(int32_t) * capacity, true));
  if (!offsets) offsets = static_cast<int32_t*>(malloc(sizeof(int32_t) * (capacity + 1)));
  if (!separators) separators = static_cast<int32_t*>(malloc(sizeof(int32_t) * capacity));
  if (!offsets || !separators) { free(offsets); free(separators); return false; }
  if (s_chat_virt.offsets && s_chat_virt.n <= s_chat_virt.offset_capacity) {
    memcpy(offsets, s_chat_virt.offsets, sizeof(int32_t) * (s_chat_virt.n + 1));
    memcpy(separators, s_chat_virt.day_sep_y, sizeof(int32_t) * s_chat_virt.n);
  }
  chatVirtFreeOffsets();
  s_chat_virt.offsets = offsets; s_chat_virt.day_sep_y = separators; s_chat_virt.offset_capacity = capacity;
  return true;
}

#if defined(TLORA_PAGER)
// Encoder-nav focus survival across the virtualized re-render. The render
// deletes and recreates every bubble row, so a row pointer cannot preserve
// focus across the rebuild. The pending logical index below is the message the
// encoder is steering toward: chatVirtRenderWindow safely detaches the group
// before destruction, then re-aims focus at the matching recreated row via the
// existing one-shot s_nav_focus_hint mechanism. pagerEncoderChatEdgeScroll sets
// the target (an edge detent selects the neighboring logical index) and folds
// further detents into it while a render is in flight — fast turning otherwise
// stepped focus out from the transiently wrong focus position before the load
// landed. -1 = idle. The timestamp expires a stale pending target (see the
// clamp) so a render that never fires can't wedge encoder nav.
#endif
#if defined(HAS_M9_KEYBOARD)
// M9 Up/Down follows chronological message indices instead of 2D bubble
// geometry. Keep the requested index across a virtual-window rebuild so an
// adjacent message just outside the materialized rows receives focus too.
#endif

void chatVirtReset(LvChatPanel *p) {
  chatVirtCancelRenderTimer();
  (void)p;
#if defined(TLORA_PAGER)
  focusRequest.index = -1;
#endif
#if defined(HAS_M9_KEYBOARD)
  focusRequest.index = -1;
#endif
  // Null the divider pointer WITHOUT queueing a delete. It is always a child of
  // p->msgs, and every path that follows a reset (lv_obj_clean in the empty-thread
  // branches, chatVirtPurgeMsgsChildrenSync) deletes
  // the widget itself. Queueing lv_obj_del_async here handed LVGL a raw pointer
  // that those synchronous cleans freed FIRST, so the deferred lv_obj_del then ran
  // on freed memory (and with both panels open, on the OTHER panel's divider).
  s_chat_virt.divider = nullptr;
  s_chat_virt.spacer = nullptr;
  s_chat_virt.panel = nullptr;
  s_chat_virt.n = 0;
  s_chat_virt.divider_i = -1;
  s_chat_virt.divider_y = -1;
  s_chat_virt.first_ring = -1;
  s_chat_virt.last_ring = -1;
  s_chat_virt.compact_chat = false;
  s_chat_virt.compact_thread_name[0] = '\0';
  s_chat_virt.virt_total_h = 0;
  s_chat_virt.lv_total_h = 0;
  s_chat_virt.last_i0 = -1;
  s_chat_virt.last_i1 = -1;
  s_chat_virt.pending_scroll = false;
  s_chat_virt.pending_scroll_bottom = false;
  s_chat_virt.pending_scroll_y = 0;
  s_chat_virt.scroll_virt_top = 0;
  s_chat_virt.scroll_lv_anchor = 0;
  s_chat_virt.scroll_virt_valid = false;
  if (host.trackScroll)
    host.trackScroll(nullptr);
  chatVirtFreeOffsets();
}

static void chatVirtEnsureMsgIdx() {
  const int need = (host.ready()) ? host.messageCapacity() : MessageTypes::MAX_UI_MESSAGES;
  if (need <= 0)
    return;
  if (s_chat_msg_idx && s_chat_msg_idx_cap == need)
    return;
  if (s_chat_msg_idx) {
    ui::platform::release(s_chat_msg_idx);
    s_chat_msg_idx = nullptr;
  }
  s_chat_msg_idx_cap = 0;
  ui::platform::release(heightCache); heightCache = nullptr;
  s_chat_msg_idx = (int *)ui::platform::allocate(sizeof(int) * (size_t)need, true);
  if (!s_chat_msg_idx)
    s_chat_msg_idx = (int *)ui::platform::allocate(sizeof(int) * (size_t)need, false);
  if (s_chat_msg_idx) {
    s_chat_msg_idx_cap = need;
    heightCache = static_cast<HeightEntry*>(ui::platform::allocate(sizeof(HeightEntry) * need, true));
    if (!heightCache) heightCache = static_cast<HeightEntry*>(ui::platform::allocate(sizeof(HeightEntry) * need, false));
    if (heightCache) memset(heightCache, 0, sizeof(HeightEntry) * need);
  }
}

static void chatParseMessageDisplay(const MessageTypes::UIMessage &m, bool channel_mode, bool thread_is_room,
                                    ChatBubbleDisplay &d) {
  d.show_sender = m.sender;
  d.show_text = m.text;
  d.retro_sender[0] = '\0';
  if (!m.outgoing &&
      (thread_is_room || (channel_mode && (m.sender[0] == '\0' || (m.sender[0] == 'r' && m.sender[1] == 'x' &&
                                                                   m.sender[2] == '\0'))))) {
    const char *body = ChannelSenderSplit::split(m.text, d.retro_sender, sizeof(d.retro_sender));
    if (d.retro_sender[0]) {
      d.show_sender = d.retro_sender;
      d.show_text = body;
    }
  }
  host.sanitize(&font12(), d.san_sender, sizeof(d.san_sender), d.show_sender);
  host.sanitize(chatMessageFont(), d.san_text, sizeof(d.san_text), d.show_text);
}

// ---- Chat virt serial diagnostics -------------------------------------------
#if TRACE_MESSAGE_SCROLL_ACTIVITY
static bool s_chat_virt_at_store_top = false;
static bool s_chat_virt_at_store_bottom = false;

static void chatVirtGetThreadName(char *name, size_t len) {
  if (!name || len == 0)
    return;
  name[0] = '\0';
  if (!host.ready())
    return;
  host.activeThreadName(name, len);
}

static const char *chatVirtSenderLabel(const MessageTypes::UIMessage &m, bool channel_mode,
                                       ChatBubbleDisplay &d) {
  chatParseMessageDisplay(m, channel_mode, s_chat_virt.thread_is_room, d);
  if (d.show_sender && d.show_sender[0])
    return d.show_sender;
  if (m.outgoing)
    return "(me)";
  return "?";
}

static void chatVirtLogMsg(const char *tag, int ordinal_1based, int ring_idx, bool channel_mode) {
  if (!tag || ordinal_1based <= 0)
    return;
  MessageTypes::UIMessage m{};
  if (!host.ready() || !host.messageAt(ring_idx, m)) {
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] %s #%d fetch_failed ring_idx=%d\n", tag, ordinal_1based, ring_idx);
    return;
  }
  char ts[24];
  formatBubbleTs(m.ts, ts, sizeof(ts));
  ChatBubbleDisplay d{};
  const char *sender = chatVirtSenderLabel(m, channel_mode, d);
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] %s #%d %s %s\n", tag, ordinal_1based, ts[0] ? ts : "--:--", sender);
}

static void chatVirtLogVisibleRange(LvChatPanel *p, int i0, int i1) {
  if (!p)
    return;
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] on_screen [%d..%d] of %d\n", i0 + 1, i1 + 1, s_chat_virt.n);
  for (int i = i0; i <= i1; ++i)
    chatVirtLogMsg("visible", i + 1, s_chat_virt.msg_idx[i], p->channel_mode);
}

static void chatVirtLogScrollTransition(LvChatPanel *p, int old_i0, int old_i1, int new_i0, int new_i1) {
  if (!p || old_i0 < 0 || old_i1 < 0)
    return;
  if (old_i0 == new_i0 && old_i1 == new_i1)
    return;
  if (new_i1 > old_i1 || new_i0 > old_i0)
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] vertical scroll up — newer messages coming on screen\n");
  else if (new_i1 < old_i1 || new_i0 < old_i0)
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] vertical scroll down — older messages coming on screen\n");
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] prefetching messages [%d..%d]\n", new_i0 + 1, new_i1 + 1);
  for (int i = old_i0; i <= old_i1; ++i) {
    if (i < new_i0 || i > new_i1)
      chatVirtLogMsg("leaving", i + 1, s_chat_virt.msg_idx[i], p->channel_mode);
  }
  for (int i = new_i0; i <= new_i1; ++i) {
    if (i < old_i0 || i > old_i1)
      chatVirtLogMsg("entering", i + 1, s_chat_virt.msg_idx[i], p->channel_mode);
  }
}

static void chatVirtCheckStoreEdges(LvChatPanel *p) {
  if (!p || !p->msgs || s_chat_virt.panel != p || s_chat_virt.n <= 0)
    return;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(p->msgs);
  const lv_coord_t sb = lv_obj_get_scroll_bottom(p->msgs);
  const bool at_top = (s_chat_virt.last_i0 == 0 && scroll_y < 16);
  const bool at_bot = (s_chat_virt.last_i1 >= s_chat_virt.n - 1 && sb <= 24);
  if (at_top && !s_chat_virt_at_store_top) {
    s_chat_virt_at_store_top = true;
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] reached store top (oldest message #1)\n");
  } else if (!at_top) {
    s_chat_virt_at_store_top = false;
  }
  if (at_bot && !s_chat_virt_at_store_bottom) {
    s_chat_virt_at_store_bottom = true;
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] reached store bottom (newest message #%d)\n", s_chat_virt.n);
  } else if (!at_bot) {
    s_chat_virt_at_store_bottom = false;
  }
}
#endif

static void chatBuildBubbleMeta(const MessageTypes::UIMessage &m, bool channel_mode, char *out,
                                size_t out_len, uint32_t *out_fg) {
  if (out && out_len > 0)
    out[0] = '\0';
  if (out_fg)
    *out_fg = colors().COLOR_SUB;
  if (!out || out_len == 0)
    return;

  char ts_buf[20];
  formatBubbleTs(m.ts, ts_buf, sizeof(ts_buf));

  const char *deliv_glyph = "";
  uint32_t deliv_fg = colors().COLOR_SUB;
  if (m.outgoing && !channel_mode && m.deliv_state != MessageTypes::DELIV_NONE) {
    switch (m.deliv_state) {
    case MessageTypes::DELIV_SENT:
      deliv_glyph = " " LV_SYMBOL_OK;
      deliv_fg = colors().COLOR_SUB;
      break;
    case MessageTypes::DELIV_DELIVERED:
      deliv_glyph = " " LV_SYMBOL_OK LV_SYMBOL_OK;
      deliv_fg = colors().COLOR_ACCENT;
      break;
    case MessageTypes::DELIV_FAILED:
      deliv_glyph = " " LV_SYMBOL_CLOSE " tap to resend";
      deliv_fg = 0xE08080;
      break;
    default:
      break;
    }
  }

  char rep_buf[12] = "";
  if (m.outgoing && m.sent_fp) {
    const uint8_t reps = host.repeats(m.sent_fp);
    if (reps > 0)
      snprintf(rep_buf, sizeof(rep_buf), " " LV_SYMBOL_REFRESH "%u", (unsigned)reps);
  } else if (!m.outgoing && (m.meta_flags & MessageTypes::MSG_META_HAS_RX) &&
             (m.meta_flags & MessageTypes::MSG_META_IS_FLOOD)) {
    const uint8_t hops = static_cast<uint8_t>(m.path_len & 0x3F);
    snprintf(rep_buf, sizeof(rep_buf), " " LV_SYMBOL_SHUFFLE "%u", (unsigned)hops);
  }

  snprintf(out, out_len, "%s%s%s", ts_buf, deliv_glyph, rep_buf);
  if (out_fg)
    *out_fg = isDay() ? colors().COLOR_CHAT_META : (deliv_glyph[0] ? deliv_fg : colors().COLOR_SUB);
}

static lv_coord_t chatTextWidth(const char *s) {
  if (!s || !s[0])
    return 0;
  lv_point_t size;
  lv_txt_get_size(&size, s, &font12(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

static bool chatUtf8Continuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

static void chatFitLeadingEllipsis(const char *src, lv_coord_t max_w, char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!src || !src[0] || max_w <= 0)
    return;
  if (chatTextWidth(src) <= max_w) {
    snprintf(out, out_len, "%s", src);
    return;
  }

  const char *ell = "...";
  if (chatTextWidth(ell) > max_w)
    return;

  const size_t len = strlen(src);
  for (size_t i = 0; i < len; ++i) {
    if (chatUtf8Continuation(src[i]))
      continue;
    char cand[48];
    snprintf(cand, sizeof(cand), "%s%s", ell, src + i);
    if (chatTextWidth(cand) <= max_w) {
      snprintf(out, out_len, "%s", cand);
      return;
    }
  }
  snprintf(out, out_len, "%s", ell);
}

static lv_coord_t chatMeasureBubbleHeight(const MessageTypes::UIMessage &m, bool channel_mode,
                                          bool thread_is_room, lv_coord_t bubble_max_w) {
  ChatBubbleDisplay d{};
  chatParseMessageDisplay(m, channel_mode, thread_is_room, d);
  const lv_coord_t inner_max_w = bubble_max_w - 2 * kChatBubblePadH;
  lv_coord_t inner_y = 0;
  char meta_buf[48];
  chatBuildBubbleMeta(m, channel_mode, meta_buf, sizeof(meta_buf), nullptr);
  // Bubble layout: timestamp/meta always share the top row (channels, DMs, rooms).
  const bool show_sender = (channel_mode || thread_is_room) && !m.outgoing && d.san_sender[0];
  if (show_sender || meta_buf[0])
    inner_y += lv_font_get_line_height(&font12());

  const lv_font_t *msg_font = chatMessageFont();
  lv_point_t txt_size;
  lv_txt_get_size(&txt_size, d.san_text, msg_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const lv_coord_t txt_w_used = (txt_size.x <= inner_max_w) ? txt_size.x : inner_max_w;
  lv_point_t wrapped_size;
  lv_txt_get_size(&wrapped_size, d.san_text, msg_font, 0, 0, txt_w_used > 0 ? txt_w_used : LV_COORD_MAX,
                  LV_TEXT_FLAG_NONE);

  return kChatBubblePadV * 2 + inner_y + wrapped_size.y;
}

static void chatBuildCompactLine(const MessageTypes::UIMessage &m, LvChatPanel *p, int logical_i,
                                 const ChatBubbleDisplay &d, char *line, size_t line_cap) {
  if (!line || line_cap == 0)
    return;
  line[0] = '\0';
#if defined(HAS_TDECK_PRO)
  const bool colorful_bubbles = false;
#else
  const bool colorful_bubbles = touchPrefsGetColorfulBubbles();
#endif
  lv_color_t dark_sender_col = lv_color_hex(colors().COLOR_RECV_BG);
  lv_color_t sender_col = lv_color_hex(colors().COLOR_ACCENT);
  const char *color_name = m.outgoing ? host.nodeName() : d.show_sender;
  if (colorful_bubbles && color_name && color_name[0]) {
    usernameBubbleColors(color_name, &dark_sender_col, &sender_col);
    if (isDay())
      sender_col = dark_sender_col;
  }

  const char *row_name =
      m.outgoing ? host.nodeName()
      : (d.san_sender[0] && !(d.san_sender[0] == 'r' && d.san_sender[1] == 'x' && d.san_sender[2] == '\0'))
          ? d.san_sender
          : s_chat_virt.compact_thread_name;
  char name_san[48];
  host.sanitize(&font12(), name_san, sizeof(name_san), row_name);
  char esc_name[100];
  char esc_text[2 * sizeof(d.san_text)];
  recolorEscape(esc_name, sizeof(esc_name), name_san);
  recolorEscape(esc_text, sizeof(esc_text), d.san_text);

  char ts_c[12];
  formatBubbleHhMm(m.ts, ts_c, sizeof(ts_c));
  const char *dglyph = "";
  uint32_t dfg = colors().COLOR_SUB;
  if (m.outgoing && !p->channel_mode && m.deliv_state != MessageTypes::DELIV_NONE) {
    switch (m.deliv_state) {
    case MessageTypes::DELIV_SENT:
      dglyph = LV_SYMBOL_UPLOAD;
      dfg = colors().COLOR_SUB;
      break;
    case MessageTypes::DELIV_DELIVERED:
      dglyph = LV_SYMBOL_OK LV_SYMBOL_OK;
      dfg = colors().COLOR_ACCENT;
      break;
    case MessageTypes::DELIV_FAILED:
      dglyph = LV_SYMBOL_CLOSE " tap to resend";
      dfg = isDay() ? colors().COLOR_STATUS_DANGER_TEXT : 0xE08080;
      break;
    }
  }
  char reps[12] = "";
  if (m.outgoing && m.sent_fp) {
    const uint8_t r = host.repeats(m.sent_fp);
    if (r > 0)
      snprintf(reps, sizeof(reps), LV_SYMBOL_REFRESH "%u", (unsigned)r);
  } else if (!m.outgoing && (m.meta_flags & MessageTypes::MSG_META_HAS_RX) &&
             (m.meta_flags & MessageTypes::MSG_META_IS_FLOOD)) {
    const uint8_t hops = (uint8_t)(m.path_len & 0x3F);
    snprintf(reps, sizeof(reps), LV_SYMBOL_SHUFFLE "%u", (unsigned)hops);
  }

  const unsigned sc_hex = lv_color_to32(sender_col) & 0xFFFFFFu;
  int off = 0;
  if (ts_c[0])
    off +=
        snprintf(line + off, line_cap - off, "#%06X %s# ", (unsigned)(colors().COLOR_SUB & 0xFFFFFFu), ts_c);
  off += snprintf(line + off, line_cap - off, "#%06X %s:# %s", sc_hex, esc_name, esc_text);
  if (off > (int)line_cap - 1)
    off = (int)line_cap - 1;
  if ((dglyph[0] || reps[0]) && off < (int)line_cap - 32)
    snprintf(line + off, line_cap - off, "  #%06X %s%s#", (unsigned)(dfg & 0xFFFFFFu), dglyph, reps);
}

// Plain-text line for height measurement — must match the glyphs/wrap of the recolor
// label (lv_txt_get_size mishandles #RRGGBB markup and full widget width).
static void chatBuildCompactPlainLine(const MessageTypes::UIMessage &m, LvChatPanel *p,
                                      const ChatBubbleDisplay &d, char *line, size_t line_cap) {
  if (!line || line_cap == 0)
    return;
  line[0] = '\0';

  const char *row_name =
      m.outgoing ? host.nodeName()
      : (d.san_sender[0] && !(d.san_sender[0] == 'r' && d.san_sender[1] == 'x' && d.san_sender[2] == '\0'))
          ? d.san_sender
          : s_chat_virt.compact_thread_name;
  char name_san[48];
  host.sanitize(&font12(), name_san, sizeof(name_san), row_name);

  char ts_c[12];
  formatBubbleHhMm(m.ts, ts_c, sizeof(ts_c));
  const char *dglyph = "";
  if (m.outgoing && !p->channel_mode && m.deliv_state != MessageTypes::DELIV_NONE) {
    switch (m.deliv_state) {
    case MessageTypes::DELIV_SENT:
      dglyph = LV_SYMBOL_UPLOAD;
      break;
    case MessageTypes::DELIV_DELIVERED:
      dglyph = LV_SYMBOL_OK LV_SYMBOL_OK;
      break;
    case MessageTypes::DELIV_FAILED:
      dglyph = LV_SYMBOL_CLOSE " tap to resend";
      break;
    default:
      break;
    }
  }
  char reps[12] = "";
  if (m.outgoing && m.sent_fp) {
    const uint8_t r = host.repeats(m.sent_fp);
    if (r > 0)
      snprintf(reps, sizeof(reps), LV_SYMBOL_REFRESH "%u", (unsigned)r);
  } else if (!m.outgoing && (m.meta_flags & MessageTypes::MSG_META_HAS_RX) &&
             (m.meta_flags & MessageTypes::MSG_META_IS_FLOOD)) {
    const uint8_t hops = (uint8_t)(m.path_len & 0x3F);
    snprintf(reps, sizeof(reps), LV_SYMBOL_SHUFFLE "%u", (unsigned)hops);
  }

  int off = 0;
  if (ts_c[0])
    off += snprintf(line + off, line_cap - off, "%s ", ts_c);
  off += snprintf(line + off, line_cap - off, "%s: %s", name_san, d.san_text);
  if (off > (int)line_cap - 1)
    off = (int)line_cap - 1;
  if ((dglyph[0] || reps[0]) && off < (int)line_cap - 32)
    snprintf(line + off, line_cap - off, "  %s%s", dglyph, reps);
}

static lv_coord_t chatMeasureCompactRowHeight(const MessageTypes::UIMessage &m, LvChatPanel *p, int logical_i,
                                              const ChatBubbleDisplay &d) {
  (void)logical_i;
  char line[640];
  chatBuildCompactPlainLine(m, p, d, line, sizeof(line));
  static constexpr lv_coord_t kPadH = 3;
  static constexpr lv_coord_t kPadV = 1;
  const lv_coord_t inner_w =
      (s_chat_virt.content_w > kPadH * 2) ? s_chat_virt.content_w - kPadH * 2 : s_chat_virt.content_w;
  lv_point_t wrapped;
  lv_txt_get_size(&wrapped, line, chatMessageFont(), 0, 0, inner_w > 0 ? inner_w : LV_COORD_MAX,
                  LV_TEXT_FLAG_NONE);
  return wrapped.y + kPadV * 2;
}

static lv_coord_t chatMeasureMessageRowHeight(const MessageTypes::UIMessage &m, LvChatPanel *p,
                                              int logical_i) {
  const int ring = s_chat_msg_idx && logical_i >= 0 && logical_i < s_chat_msg_idx_cap ? s_chat_msg_idx[logical_i] : -1;
  HeightEntry* cached = heightCache && ring >= 0 && ring < s_chat_msg_idx_cap ? &heightCache[ring] : nullptr;
  const uint32_t signature = contentSignature(m);
  if (cached && cached->sequence == m.seq && cached->signature == signature && cached->height > 0) return cached->height;
  ++work.heightMeasurements;
  lv_coord_t height;
  if (s_chat_virt.compact_chat) {
    ChatBubbleDisplay d{};
    chatParseMessageDisplay(m, p->channel_mode, s_chat_virt.thread_is_room, d);
    height = chatMeasureCompactRowHeight(m, p, logical_i, d);
  } else height = chatMeasureBubbleHeight(m, p->channel_mode, s_chat_virt.thread_is_room, s_chat_virt.bubble_max_w);
  if (cached) { cached->sequence = m.seq; cached->signature = signature; cached->height = height; }
  return height;
}

// Safe teardown for floating chat widgets — same pattern as host.popupClose: if the
// msgs scroller is (or was) the active scroll object, synchronous lv_obj_del
// during the indev/draw tick can leave LVGL drawing freed memory (heap assert in
// lv_mem_buf_get / draw_shadow on open of a large channel).
static void chatVirtBeforeMassDelete() {
  lv_indev_t *act = lv_indev_get_act();
  if (act)
    lv_indev_wait_release(act);
}

// Sync purge — only call from lv_async_call / timer context, not indev handlers.
// Returns true when keypad navigation was detached and must be rebuilt after the
// replacement tree (normally a placeholder) has been created.
static bool chatVirtPurgeMsgsChildrenSync(LvChatPanel *p) {
  if (!p || !p->msgs)
    return false;
  chatVirtCancelRenderTimer();
#if CAP_KEYPAD_NAV
  const bool nav_detached = host.detachNavigation();
#else
  constexpr bool nav_detached = false;
#endif
  chatVirtResetInputForMsgs(p);
  chatVirtBeforeMassDelete();
  for (int i = static_cast<int>(lv_obj_get_child_cnt(p->msgs)) - 1; i >= 0; --i) {
    lv_obj_t *ch = lv_obj_get_child(p->msgs, i);
    if (ch)
      lv_obj_del(ch);
  }
  s_chat_virt.spacer = nullptr;
  s_chat_virt.divider = nullptr;
  return nav_detached;
}

static void chatVirtResetToPlaceholder(LvChatPanel &p, const char *text) {
  lv_indev_reset(nullptr, nullptr);
  chatVirtReset(&p);
  const bool nav_detached = chatVirtPurgeMsgsChildrenSync(&p);
  chatDetailShowPlaceholder(p, text);
#if CAP_KEYPAD_NAV
  if (nav_detached)
    host.rebuildNavigation();
#else
  (void)nav_detached;
#endif
}

static void chatVirtEnsureSpacer(LvChatPanel *p, lv_coord_t total_h) {
  if (!p || !p->msgs)
    return;
  if (!s_chat_virt.spacer || !lv_obj_is_valid(s_chat_virt.spacer)) {
    s_chat_virt.spacer = lv_obj_create(p->msgs);
    lv_obj_remove_style_all(s_chat_virt.spacer);
    lv_obj_clear_flag(s_chat_virt.spacer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(s_chat_virt.spacer, s_chat_virt.content_w > 0 ? s_chat_virt.content_w : 1);
    lv_obj_move_background(s_chat_virt.spacer);
    s_chat_virt.spacer_w = s_chat_virt.spacer_h = 0;
  }
  const lv_coord_t width = s_chat_virt.content_w > 0 ? s_chat_virt.content_w : 1;
  const lv_coord_t height = total_h > 0 ? total_h : 1;
  if (s_chat_virt.spacer_w != width || s_chat_virt.spacer_h != height) {
    lv_obj_set_size(s_chat_virt.spacer, width, height);
    s_chat_virt.spacer_w = width;
    s_chat_virt.spacer_h = height;
  }
}

void chatVirtRefreshScrollArea(LvChatPanel *p) {
  if (!p || !p->msgs || !s_chat_virt.offsets || s_chat_virt.n <= 0)
    return;
  chatVirtEnsureSpacer(p, s_chat_virt.lv_total_h);
}

// Reposition floating bubble/divider children in viewport coords (virt px 1:1).
// LVGL scroll_y may be compressed; virt_top comes from chatVirtEffectiveVirtTop().
// Only the spacer contributes to scroll extent; bubbles must not expand it.
void chatVirtSyncBubblePositions(LvChatPanel *p) {
  if (!p || !p->msgs || s_chat_virt.panel != p || s_chat_virt.n <= 0 || !s_chat_virt.offsets)
    return;

  struct BubbleEntry {
    lv_obj_t *obj;
    int logical_i;
    lv_coord_t h;
  };
  BubbleEntry entries[128];
  int cnt = 0;

  for (uint32_t ci = 0; ci < lv_obj_get_child_cnt(p->msgs) && cnt < 128; ++ci) {
    lv_obj_t *ch = lv_obj_get_child(p->msgs, ci);
    if (!ch || ch == s_chat_virt.spacer || lv_obj_has_flag(ch, LV_OBJ_FLAG_HIDDEN))
      continue;
    if (s_chat_virt.divider && ch == s_chat_virt.divider)
      continue;
    const intptr_t ud = reinterpret_cast<intptr_t>(lv_obj_get_user_data(ch));
    if (ud < 0) {
      const int logical_i = static_cast<int>(-(ud + 1));
      if (logical_i >= 0 && logical_i < s_chat_virt.n && s_chat_virt.day_sep_y &&
          s_chat_virt.day_sep_y[logical_i] >= 0) {
        const int32_t virt_top = chatVirtEffectiveVirtTop(p);
        const lv_coord_t vp = static_cast<lv_coord_t>(s_chat_virt.day_sep_y[logical_i] - virt_top);
        lv_obj_set_pos(ch, 0, vp + 2);
      }
      continue;
    }
    if (ud >= s_chat_virt.n)
      continue;
    entries[cnt].obj = ch;
    entries[cnt].logical_i = static_cast<int>(ud);
    entries[cnt].h = lv_obj_get_height(ch);
    ++cnt;
  }
  if (cnt == 0) {
    if (s_chat_virt.divider && lv_obj_is_valid(s_chat_virt.divider) && s_chat_virt.divider_y >= 0) {
      const int32_t virt_top = chatVirtEffectiveVirtTop(p);
      const lv_coord_t div_vp = static_cast<lv_coord_t>(s_chat_virt.divider_y - virt_top);
      lv_obj_set_pos(s_chat_virt.divider, 0, div_vp);
    }
    return;
  }

  for (int i = 1; i < cnt; ++i) {
    BubbleEntry tmp = entries[i];
    int j = i - 1;
    while (j >= 0 && entries[j].logical_i > tmp.logical_i) {
      entries[j + 1] = entries[j];
      --j;
    }
    entries[j + 1] = tmp;
  }

  const int32_t virt_top = chatVirtEffectiveVirtTop(p);
  for (int i = 0; i < cnt; ++i) {
    const lv_coord_t vp = chatVirtMsgViewportY(entries[i].logical_i, virt_top);
    // Re-assert the correct left/right x on every pass instead of preserving lv_obj_get_x():
    // a bubble recreated mid-scroll (e.g. right after a send) can briefly carry a wrong x, and
    // the old preserve-x logic then locked it in until the next full rebuild — that was the
    // "just-sent outgoing bubble stuck on the LEFT" bug. Compact rows stay full-width.
    lv_coord_t x = lv_obj_get_x(entries[i].obj);
    if (!s_chat_virt.compact_chat && host.ready() && s_chat_virt.msg_idx && entries[i].logical_i >= 0 &&
        entries[i].logical_i < s_chat_virt.n) {
      MessageTypes::UIMessage mm;
      if (host.messageAt(s_chat_virt.msg_idx[entries[i].logical_i], mm)) {
        lv_coord_t w = lv_obj_get_width(entries[i].obj);
        if (w > s_chat_virt.bubble_max_w)
          w = s_chat_virt.bubble_max_w;
        x = mm.outgoing ? (s_chat_virt.content_w - w - kChatSideGutter) : kChatSideGutter;
      }
    }
    lv_obj_set_pos(entries[i].obj, x, vp);
  }

  if (s_chat_virt.divider && lv_obj_is_valid(s_chat_virt.divider) && s_chat_virt.divider_y >= 0) {
    const lv_coord_t div_vp = static_cast<lv_coord_t>(s_chat_virt.divider_y - virt_top);
    lv_obj_set_pos(s_chat_virt.divider, 0, div_vp);
  }
}

// Fast path for the by-far most common relayout trigger: new messages appended to
// the SAME thread with no divider in play. Extends offsets[]/day_sep_y[] for the
// new tail only instead of re-measuring the whole thread — the full measure is
// 2-3 lv_txt_get_size calls per message, which on the 5000-message SD ring costs
// north of 100 ms of UI-thread stall PER RECEIVED MESSAGE with the chat open.
// Endpoint ring slots prove "pure append": if the first n_old entries were evicted
// or rotated, first_ring/last_ring moved and we fall back to the full rebuild.
static bool chatVirtTryAppendLayout(LvChatPanel *p, int n, int divider_i) {
  const int n_old = s_chat_virt.n;
  if (s_chat_virt.panel != p || !s_chat_virt.offsets || !s_chat_virt.day_sep_y)
    return false;
  if (n_old <= 0 || n <= n_old)
    return false;
  if (divider_i >= 0 || s_chat_virt.divider_i >= 0)
    return false; // divider math: full rebuild
  if (s_chat_virt.compact_chat != touchPrefsGetCompactChat())
    return false;
  if (s_chat_msg_idx[0] != s_chat_virt.first_ring || s_chat_msg_idx[n_old - 1] != s_chat_virt.last_ring)
    return false;

  if (!ensureOffsetCapacity(n)) return false;
  int32_t* offs = s_chat_virt.offsets;
  int32_t* seps = s_chat_virt.day_sep_y;

  // Day-key continuity: the appended range needs the day of the last old message.
  long last_day_key = -1;
  {
    MessageTypes::UIMessage m;
    long dk = 0;
    if (host.messageAt(s_chat_msg_idx[n_old - 1], m) && chatMsgDayKey(m, &dk))
      last_day_key = dk;
  }

  const lv_coord_t row_gap = s_chat_virt.compact_chat ? kChatCompactRowGap : kChatRowGap;
  const lv_coord_t day_sep_h = chatMeasureDaySepHeight();
  int32_t y = offs[n_old]; // continue exactly where the old layout ended
  for (int i = n_old; i < n; ++i) {
    seps[i] = -1;
    MessageTypes::UIMessage m;
    if (!host.messageAt(s_chat_msg_idx[i], m)) {
      offs[i] = y;
      y += 20 + row_gap;
      continue;
    }
    long dk = 0;
    if (chatMsgDayKey(m, &dk) && dk != last_day_key) {
      last_day_key = dk;
      seps[i] = y;
      y += day_sep_h;
    }
    offs[i] = y;
    y += chatMeasureMessageRowHeight(m, p, i) + row_gap;
  }
  offs[n] = y;

  s_chat_virt.n = n;
  s_chat_virt.msg_idx = s_chat_msg_idx;
  s_chat_virt.first_ring = s_chat_msg_idx[0];
  s_chat_virt.last_ring = s_chat_msg_idx[n - 1];
  chatVirtUpdateLvScale(p, n, y); // total grew: recompute the compression
  return true;
}

static bool chatVirtRebuildLayout(LvChatPanel *p, int n, int divider_i) {
  if (!p || !host.ready() || n <= 0 || !s_chat_msg_idx)
    return false;
  if (chatVirtTryAppendLayout(p, n, divider_i))
    return true;
  if (!ensureOffsetCapacity(n)) return false;
  for (int i = 0; i < n; ++i)
    s_chat_virt.day_sep_y[i] = -1;

  s_chat_virt.panel = p;
  s_chat_virt.n = n;
  s_chat_virt.divider_i = divider_i;
  s_chat_virt.divider_y = -1;
  s_chat_virt.msg_idx = s_chat_msg_idx;
  s_chat_virt.first_ring = s_chat_msg_idx[0];
  s_chat_virt.last_ring = s_chat_msg_idx[n - 1];
  s_chat_virt.content_w = lv_disp_get_hor_res(nullptr) - 12;
  s_chat_virt.bubble_max_w = (s_chat_virt.content_w * 80) / 100;
  s_chat_virt.thread_is_room = false;
  if (!p->channel_mode) {
    s_chat_virt.thread_is_room = host.activeThreadIsRoom();
  }
  s_chat_virt.compact_chat = touchPrefsGetCompactChat();
  s_chat_virt.compact_thread_name[0] = '\0';
  if (s_chat_virt.compact_chat) {
    host.activeThreadName(s_chat_virt.compact_thread_name, sizeof(s_chat_virt.compact_thread_name));
  }

  const lv_coord_t row_gap = s_chat_virt.compact_chat ? kChatCompactRowGap : kChatRowGap;
  const lv_coord_t day_sep_h = chatMeasureDaySepHeight();
  int32_t y = 0;
  long last_day_key = -1;
#if TRACE_MESSAGE_SCROLL_ACTIVITY
  int measure_fail = 0;
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] measuring layout for %d messages\n", n);
#endif
  for (int i = 0; i < n; ++i) {
    if (divider_i >= 0 && i == divider_i) {
      s_chat_virt.divider_y = y;
      y += kChatDividerH + row_gap;
    }
    MessageTypes::UIMessage m;
    if (!host.messageAt(s_chat_msg_idx[i], m)) {
      s_chat_virt.offsets[i] = y;
      y += 20 + row_gap;
#if TRACE_MESSAGE_SCROLL_ACTIVITY
      ++measure_fail;
#endif
      continue;
    }
    long dk = 0;
    if (chatMsgDayKey(m, &dk) && dk != last_day_key) {
      last_day_key = dk;
      s_chat_virt.day_sep_y[i] = y;
      y += day_sep_h;
    }
    s_chat_virt.offsets[i] = y;
    y += chatMeasureMessageRowHeight(m, p, i) + row_gap;
  }
  s_chat_virt.offsets[n] = y;
  chatVirtUpdateLvScale(p, n, y);
#if TRACE_MESSAGE_SCROLL_ACTIVITY
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] layout total_height=%d px lv_scroll_h=%d max_scroll~=%d%s\n", (int)y,
                           (int)s_chat_virt.lv_total_h,
                           (int)(s_chat_virt.lv_total_h > chatVirtMsgsViewH(p)
                                     ? s_chat_virt.lv_total_h - chatVirtMsgsViewH(p)
                                     : 0),
                           chatVirtCompressCoords() ? " (compressed)" : "");
  if (measure_fail > 0)
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] layout measure failures=%d\n", measure_fail);
#endif
  host.trackScroll(p->msgs);
  return true;
}

static void chatVirtCreateDivider(LvChatPanel *p, lv_coord_t vp_y) {
  if (!p || !p->msgs)
    return;
  if (s_chat_virt.divider && lv_obj_is_valid(s_chat_virt.divider)) {
    lv_obj_clear_flag(s_chat_virt.divider, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const lv_coord_t kContentW = s_chat_virt.content_w;
  lv_obj_t *div = lv_obj_create(p->msgs);
  lv_obj_remove_style_all(div);
  lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(div, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(div, kContentW, kChatDividerH);
  lv_obj_set_pos(div, 0, vp_y);
  lv_obj_t *dline = lv_obj_create(div);
  lv_obj_remove_style_all(dline);
  lv_obj_set_size(dline, kContentW, 1);
  lv_obj_set_pos(dline, 0, 8);
  lv_obj_set_style_bg_color(dline, lv_color_hex(0xE0533D), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dline, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t *dlbl = lv_label_create(div);
  lv_label_set_text(dlbl, TR("New"));
  lv_obj_set_style_text_font(dlbl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(dlbl, lightSurfaceTextColor(0xE0533D), LV_PART_MAIN);
  lv_obj_set_style_bg_color(dlbl, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dlbl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(dlbl, 4, LV_PART_MAIN);
  lv_obj_align(dlbl, LV_ALIGN_TOP_LEFT, 0, 0);
  s_chat_virt.divider = div;
}

static void chatVirtCreateDaySeparator(LvChatPanel *p, int logical_i, lv_coord_t vp_y) {
  if (!p || !p->msgs || !host.ready() || !s_chat_virt.msg_idx)
    return;
  if (logical_i < 0 || logical_i >= s_chat_virt.n)
    return;
  MessageTypes::UIMessage m;
  if (!host.messageAt(s_chat_virt.msg_idx[logical_i], m))
    return;
  if (m.ts < 1577836800UL)
    return;
  time_t tt = (time_t)m.ts;
  struct tm tv;
  ui::platform::localTime(tt, tv);
  char dbuf[32];
  formatDaySeparator(dbuf, sizeof(dbuf), &tv);
  lv_obj_t *dl = nullptr;
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(p->msgs); ++c) {
    auto* child = lv_obj_get_child(p->msgs, c);
    if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(child)) < 0 &&
        lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) { dl = child; break; }
  }
  if (!dl) dl = lv_label_create(p->msgs);
  lv_obj_clear_flag(dl, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(dl, dbuf);
  lv_obj_set_style_text_font(dl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(dl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_add_flag(dl, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_width(dl, s_chat_virt.content_w);
  lv_obj_set_pos(dl, 0, vp_y + 2);
  lv_obj_set_user_data(dl, reinterpret_cast<void *>(static_cast<intptr_t>(-(logical_i + 1))));
}

// ============================ Clickable URLs in chat ================================
// Detect URLs in a message, tint them blue, and on a short tap offer "Open in web" (the
// on-device reader) or "Create QR" (an on-screen QR so a phone can grab the link).
// UNGATED (QR works on every board); the "Open in web" button compiles only where the
// reader exists.
static bool urlCiHas(const char *s, const char *pfx, int n) {
  for (int i = 0; i < n; i++) {
    char a = s[i];
    if (!a)
      return false;
    if (a >= 'A' && a <= 'Z')
      a += 32;
    if (a != pfx[i])
      return false;
  }
  return true;
}
// First URL span [*a,*b) at/after `from`. Recognises http(s):// and a bare www.<host>.
static bool chatUrlSpan(const char *s, int from, int *a, int *b) {
  for (int i = from; s[i]; i++) {
    int len = 0;
    if (urlCiHas(s + i, "https://", 8))
      len = 8;
    else if (urlCiHas(s + i, "http://", 7))
      len = 7;
    else if (urlCiHas(s + i, "www.", 4) && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\n'))
      len = 4;
    else
      continue;
    int j = i + len;
    while (s[j] && (unsigned char)s[j] > ' ')
      j++; // extend to whitespace
    while (j > i + len) {
      char c = s[j - 1]; // trim trailing punctuation
      if (c == '.' || c == ',' || c == ')' || c == ';' || c == '!' || c == '?' || c == '\'' || c == '"' ||
          c == ':')
        j--;
      else
        break;
    }
    if (j <= i + len)
      continue; // scheme, no host
    if (!memchr(s + i + len, '.', j - (i + len)))
      continue; // host needs a dot
    *a = i;
    *b = j;
    return true;
  }
  return false;
}
static bool chatFirstUrl(const char *s, char *out, int cap) {
  int a, b;
  if (!s || !chatUrlSpan(s, 0, &a, &b))
    return false;
  int n = b - a;
  if (n > cap - 1)
    n = cap - 1;
  memcpy(out, s + a, n);
  out[n] = 0;
  return true;
}
// Copy `in` -> `out`, wrapping each URL in a blue recolor tag. Bails (false) if `in`
// already has a '#' (the recolor parser would choke on it) — caller then shows plain
// text and the tap still works.
static bool chatRecolorUrls(const char *in, char *out, int cap) {
  if (!in || strchr(in, '#'))
    return false;
  int a, b;
  if (!chatUrlSpan(in, 0, &a, &b))
    return false;
  int o = 0, i = 0;
  while (in[i] && o < cap - 12) {
    if (i == a) {
      o += snprintf(out + o, cap - o, "#%06X ", (unsigned)(colors().COLOR_CHAT_LINK & 0xFFFFFFu));
      while (i < b && o < cap - 2)
        out[o++] = in[i++];
      if (o < cap - 1)
        out[o++] = '#';
      if (!chatUrlSpan(in, i, &a, &b))
        a = -1;
    } else
      out[o++] = in[i++];
  }
  out[o] = 0;
  return true;
}

// ---- QR popup: a scannable QR of a URL ----
static lv_obj_t *s_urlqr_root = nullptr;
static bool eventBelongsTo(lv_event_t *event, lv_obj_t *root) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void urlRootDeleted(lv_event_t *event) {
  auto **root = static_cast<lv_obj_t **>(lv_event_get_user_data(event));
  if (*root == lv_event_get_target(event))
    *root = nullptr;
}
void closeUrlQr() {
  if (s_urlqr_root)
    host.popupClose(&s_urlqr_root);
}
static void urlQrBackdropCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !eventBelongsTo(e, s_urlqr_root))
    return;
  lv_indev_t *a = lv_indev_get_act();
  if (a)
    lv_indev_wait_release(a);
  closeUrlQr();
}
static void openUrlQrPopup(const char *url) {
  closeUrlQr();
  lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  s_urlqr_root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(s_urlqr_root, urlRootDeleted, LV_EVENT_DELETE, &s_urlqr_root);
  lv_obj_remove_style_all(s_urlqr_root);
  lv_obj_set_size(s_urlqr_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_urlqr_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_urlqr_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_urlqr_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_urlqr_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_urlqr_root, urlQrBackdropCb, LV_EVENT_CLICKED, nullptr);
  int card_w = PCW(230);
  if (card_w > (lv_disp_get_hor_res(nullptr) - 12))
    card_w = (lv_disp_get_hor_res(nullptr) - 12);
  int card_h = PSC(250);
  if (card_h > (lv_disp_get_ver_res(nullptr) - host.statusHeight() - 12))
    card_h = (lv_disp_get_ver_res(nullptr) - host.statusHeight() - 12);
  int qr_size = card_h - 34 - 34 - 20;
  if (qr_size > PSC(160))
    qr_size = PSC(160);
  if (qr_size > card_w - 20)
    qr_size = card_w - 20;
  if (qr_size < 96)
    qr_size = 96;
  lv_obj_t *card = lv_obj_create(s_urlqr_root);
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
  addCloseXBadge(card, urlQrBackdropCb);
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR("Scan to open on phone"));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 20 - 32);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 4);
  char full[260]; // a bare www. host gets https:// so the phone opens it
  if (!urlCiHas(url, "http", 4))
    snprintf(full, sizeof full, "https://%s", url);
  else
    snprintf(full, sizeof full, "%s", url);
  lv_obj_t *qr = lv_qrcode_create(card, qr_size, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_qrcode_update(qr, full, strlen(full));
  lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(qr, 4, LV_PART_MAIN);
  lv_obj_t *u = lv_label_create(card);
  lv_label_set_long_mode(u, LV_LABEL_LONG_DOT);
  lv_obj_set_width(u, card_w - 20);
  lv_label_set_text(u, url);
  lv_obj_set_style_text_font(u, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(u, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -2);
}

// ---- URL action menu (short tap on a chat URL) ----
static lv_obj_t *s_urlmenu_root = nullptr;
static char s_urlmenu_url[240] = "";
void closeUrlMenu() {
  if (s_urlmenu_root)
    host.popupClose(&s_urlmenu_root);
}
static void urlMenuBackdropCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !eventBelongsTo(e, s_urlmenu_root))
    return;
  lv_indev_t *a = lv_indev_get_act();
  if (a)
    lv_indev_wait_release(a);
  closeUrlMenu();
}
static void urlMenuQrCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !eventBelongsTo(e, s_urlmenu_root))
    return;
  char u[240];
  snprintf(u, sizeof u, "%s", s_urlmenu_url);
  closeUrlMenu();
  openUrlQrPopup(u);
}
static void openUrlMenu(const char *url) {
  if (!url || !url[0])
    return;
  closeUrlMenu();
  snprintf(s_urlmenu_url, sizeof s_urlmenu_url, "%s", url);
  lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  s_urlmenu_root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(s_urlmenu_root, urlRootDeleted, LV_EVENT_DELETE, &s_urlmenu_root);
  lv_obj_remove_style_all(s_urlmenu_root);
  lv_obj_set_size(s_urlmenu_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_urlmenu_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_urlmenu_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_urlmenu_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_urlmenu_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_urlmenu_root, urlMenuBackdropCb, LV_EVENT_CLICKED, nullptr);
  const int card_w = PCW(230), btn_h = PSC(34), pad = PSC(12), gap = PSC(8), url_h = PSC(18);
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  const int nbtn = 2;
#else
  const int nbtn = 1;
#endif
  const int card_h = pad + url_h + gap + nbtn * btn_h + (nbtn - 1) * gap + pad;
  lv_obj_t *card = lv_obj_create(s_urlmenu_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *u = lv_label_create(card); // URL preview line at the top
  lv_label_set_long_mode(u, LV_LABEL_LONG_DOT);
  lv_obj_set_width(u, card_w - 2 * pad);
  lv_label_set_text(u, url);
  lv_obj_set_style_text_font(u, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(u, lightSurfaceTextColor(0x6FB7FF), LV_PART_MAIN);
  lv_obj_set_pos(u, 0, 0);
  int by = url_h + gap;
  auto mk = [&](const char *txt, lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, by);
    styleButton(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, TR(txt));
    lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
    by += btn_h + gap;
  };
  mk(TR(LV_SYMBOL_IMAGE "  Create QR"), urlMenuQrCb);
}
// Each rendered row carries the immutable sequence of its ring record. A slot
// may be reused before the next render; an old row must never act on its new data.
struct RowAction {
  int ringIndex;
  uint32_t sequence;
  bool url, retry;
  uint32_t pressedSequence = 0;
};
static constexpr unsigned kRowSlots = 128;
struct RowView {
  lv_obj_t *root = nullptr, *sender = nullptr, *meta = nullptr, *body = nullptr;
  RowAction* action = nullptr;
  int ring = -1, logical = -1;
  uint32_t sequence = 0, signature = 0, styleEpoch = 0;
  bool compact = false;
  lv_coord_t innerWidth = 0, metaWidth = 0;
};
static RowView rows[kRowSlots];
static RowView* buildingRow = nullptr;
static void rowDeleted(lv_event_t* event) {
  auto* row = static_cast<RowView*>(lv_event_get_user_data(event));
  if (lv_event_get_target(event) == row->root) *row = {};
}
static lv_obj_t* rowRoot(LvChatPanel* panel, bool compact) {
  auto& view = *buildingRow;
  if (!view.root) {
    view.root = compact ? lv_label_create(panel->msgs) : lv_obj_create(panel->msgs);
    view.compact = compact;
    lv_obj_add_event_cb(view.root, rowDeleted, LV_EVENT_DELETE, &view);
    ++work.rowsCreated;
    if (!compact) lv_obj_remove_style_all(view.root);
  }
  if (lv_obj_get_parent(view.root) != panel->msgs) lv_obj_set_parent(view.root, panel->msgs);
  lv_obj_clear_flag(view.root, LV_OBJ_FLAG_HIDDEN);
  ++work.rowsBound;
  return view.root;
}
static void rowActionEvent(lv_event_t *event) {
  auto *action = static_cast<RowAction *>(lv_event_get_user_data(event));
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_DELETE) {
    ui::platform::release(action);
    return;
  }
  if (code == LV_EVENT_PRESSED) { action->pressedSequence = action->sequence; return; }
  if (code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_CLICKED)
    return;
  if (!host.ready || !host.ready() || !s_chat_virt.panel || !s_chat_virt.panel->detail_open ||
      !eventBelongsTo(event, s_chat_virt.panel->msgs))
    return;
  MessageTypes::UIMessage message;
  if (!host.messageAt(action->ringIndex, message) || message.seq != action->sequence)
    return;
  if (action->pressedSequence && action->pressedSequence != action->sequence) return;
  if (code == LV_EVENT_CLICKED && chatTapToLatest(s_chat_virt.panel)) return;
  if (code == LV_EVENT_LONG_PRESSED)
    host.longPressMessage(action->ringIndex);
  else if (code == LV_EVENT_CLICKED && action->retry)
    host.retryMessage(action->ringIndex);
  else if (code == LV_EVENT_SHORT_CLICKED && action->url) {
    char url[240];
    if (chatFirstUrl(message.text, url, sizeof url))
      openUrlMenu(url);
  }
}
static void bindRowActions(lv_obj_t *row, int ringIndex, const MessageTypes::UIMessage &message, bool url) {
  auto* action = buildingRow->action;
  const bool fresh = !action;
  if (!action) action = static_cast<RowAction *>(ui::platform::allocate(sizeof(RowAction), false));
  if (!action)
    return; // A readable row is preferable to a dangling callback on OOM.
  const bool retry = message.outgoing && message.deliv_state == MessageTypes::DELIV_FAILED;
  const uint32_t pressed = fresh ? 0 : action->pressedSequence;
  *action = {ringIndex, message.seq, url && !retry, retry, pressed};
  buildingRow->action = action;
  if (fresh) lv_obj_add_event_cb(row, rowActionEvent, LV_EVENT_ALL, action);
}

static lv_coord_t chatVirtCreateBubble(LvChatPanel *p, int logical_i, int ring_idx, lv_coord_t vp_y,
                                       lv_coord_t *out_jump_y) {
  if (!p || !host.ready())
    return 0;
  MessageTypes::UIMessage m;
  if (!host.messageAt(ring_idx, m))
    return 0;

  ChatBubbleDisplay d{};
  chatParseMessageDisplay(m, p->channel_mode, s_chat_virt.thread_is_room, d);
  const lv_coord_t kContentW = s_chat_virt.content_w;
  const lv_coord_t kBubbleMaxW = s_chat_virt.bubble_max_w;
#if defined(HAS_TDECK_PRO)
  const bool colorful_bubbles = false;
#else
  const bool colorful_bubbles = touchPrefsGetColorfulBubbles();
#endif

  lv_obj_t *bubble = rowRoot(p, false);
  lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bubble, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_radius(bubble, 10, LV_PART_MAIN);
  const bool mentions_me =
      (p->channel_mode || s_chat_virt.thread_is_room) && !m.outgoing && host.mentionsMe(d.show_text);
  const char *color_name = m.outgoing ? host.nodeName() : d.show_sender;
  lv_color_t bubble_bg = lv_color_hex(m.outgoing ? colors().COLOR_CHAT_SENT_BG : colors().COLOR_CHAT_RECV_BG);
  lv_color_t sender_col = lv_color_hex(colors().COLOR_ACCENT);
  if (colorful_bubbles && color_name && color_name[0])
    usernameBubbleColors(color_name, &bubble_bg, &sender_col);
  if (mentions_me)
    bubble_bg = lv_color_hex(colors().COLOR_CHAT_MENTION_BG);
  if (isDay())
    sender_col = lv_color_hex(colors().COLOR_CHAT_TEXT);
#if defined(HAS_TDECK_PRO)
  bubble_bg = lv_color_white();
  sender_col = lv_color_black();
#endif
  lv_obj_set_style_bg_color(bubble, bubble_bg, LV_PART_MAIN);
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_bg_opa(bubble, LV_OPA_TRANSP, LV_PART_MAIN);
#else
  lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
#endif
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_border_color(bubble, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_width(bubble, 2, LV_PART_MAIN);
  lv_obj_set_style_border_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
#endif
  lv_obj_set_style_pad_hor(bubble, kChatBubblePadH, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(bubble, kChatBubblePadV, LV_PART_MAIN);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  const lv_coord_t kInnerMaxW = kBubbleMaxW - 2 * kChatBubblePadH;
  const lv_font_t *msg_font = chatMessageFont();
  lv_point_t txt_size;
  lv_txt_get_size(&txt_size, d.san_text, msg_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const lv_coord_t txt_w_used = (txt_size.x <= kInnerMaxW) ? txt_size.x : kInnerMaxW;

  char meta_buf[48];
  uint32_t meta_fg = colors().COLOR_SUB;
  chatBuildBubbleMeta(m, p->channel_mode, meta_buf, sizeof(meta_buf), &meta_fg);
#if defined(HAS_TDECK_PRO)
  meta_fg = 0x000000;
#endif
  // All bubble-style threads (channel / DM / room): timestamp + delivery meta on the top row.
  const bool show_sender_line =
      (p->channel_mode || s_chat_virt.thread_is_room) && !m.outgoing && d.san_sender[0];

  const lv_coord_t sender_w = show_sender_line ? chatTextWidth(d.san_sender) : 0;
  const lv_coord_t meta_w = meta_buf[0] ? chatTextWidth(meta_buf) : 0;
  lv_coord_t inner_w = txt_w_used > 0 ? txt_w_used : 1;
  {
    lv_coord_t header_w = (show_sender_line ? sender_w : 0) + meta_w;
    if (show_sender_line && meta_w)
      header_w += 6;
    if (header_w > kInnerMaxW)
      header_w = kInnerMaxW;
    if (header_w > inner_w)
      inner_w = header_w;
  }

  int inner_y = 0;
  if (buildingRow->sender) lv_obj_add_flag(buildingRow->sender, LV_OBJ_FLAG_HIDDEN);
  if (buildingRow->meta) lv_obj_add_flag(buildingRow->meta, LV_OBJ_FLAG_HIDDEN);
  // Analytic bubble width for x-alignment (widest of sender/text/meta) — do not use
  // lv_obj_get_width() right after create; unsettled layout can mis-place outgoing bubbles.
  if (show_sender_line || meta_buf[0]) {
    const lv_coord_t line_h = lv_font_get_line_height(&font12());
    char meta_fit[48] = "";
    lv_coord_t meta_fit_w = 0;
    lv_coord_t sender_label_w = sender_w;

    if (meta_buf[0]) {
      lv_coord_t available_meta_w = inner_w - (show_sender_line ? (sender_w + 6) : 0);
      const lv_coord_t ell_w = chatTextWidth("...");
      if (show_sender_line && available_meta_w < ell_w && inner_w > ell_w + 6) {
        sender_label_w = inner_w - ell_w - 6;
        available_meta_w = ell_w;
      }
      chatFitLeadingEllipsis(meta_buf, available_meta_w, meta_fit, sizeof(meta_fit));
      meta_fit_w = chatTextWidth(meta_fit);
    }

    if (show_sender_line && sender_label_w > 0) {
      lv_obj_t *slbl = buildingRow->sender;
      if (!slbl) buildingRow->sender = slbl = lv_label_create(bubble);
      lv_obj_clear_flag(slbl, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(slbl, d.san_sender);
      lv_obj_set_style_text_font(slbl, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(slbl, sender_col, LV_PART_MAIN);
      lv_label_set_long_mode(slbl, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(slbl, LV_SIZE_CONTENT);
      if (sender_label_w < sender_w) {
        lv_label_set_long_mode(slbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(slbl, sender_label_w);
      }
      lv_obj_set_pos(slbl, 0, inner_y);
    } else if (buildingRow->sender) lv_obj_add_flag(buildingRow->sender, LV_OBJ_FLAG_HIDDEN);
    if (meta_fit[0]) {
      lv_obj_t *mlbl = buildingRow->meta;
      if (!mlbl) buildingRow->meta = mlbl = lv_label_create(bubble);
      lv_obj_clear_flag(mlbl, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(mlbl, meta_fit);
      lv_obj_set_style_text_font(mlbl, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(mlbl, lv_color_hex(meta_fg), LV_PART_MAIN);
      lv_obj_set_pos(mlbl, inner_w - meta_fit_w, inner_y);
    } else if (buildingRow->meta) lv_obj_add_flag(buildingRow->meta, LV_OBJ_FLAG_HIDDEN);
    buildingRow->innerWidth = inner_w;
    buildingRow->metaWidth = inner_w - (show_sender_line ? sender_label_w + 6 : 0);
    inner_y += line_h;
  }

  lv_obj_t *tlbl = buildingRow->body;
  if (!tlbl) buildingRow->body = tlbl = lv_label_create(bubble);
  lv_label_set_recolor(tlbl, false);
  lv_obj_set_style_text_font(tlbl, msg_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(tlbl,
#if defined(HAS_TDECK_PRO)
                              lv_color_black(),
#else
                              lv_color_hex(colors().COLOR_CHAT_TEXT),
#endif
                              LV_PART_MAIN);
  lv_label_set_text(tlbl, d.san_text);
  // Clickable URLs: tint any link blue (recolor tags are zero-width, so wrapping/height
  // below still measure from the plain d.san_text and stay correct).
  int _ua, _ub;
  const bool has_url = chatUrlSpan(d.san_text, 0, &_ua, &_ub);
#if !defined(HAS_TDECK_PRO)
  if (has_url) {
    char rc[MessageTypes::MAX_MSG_TEXT + 40];
    if (chatRecolorUrls(d.san_text, rc, sizeof rc)) {
      lv_label_set_recolor(tlbl, true);
      lv_label_set_text(tlbl, rc);
    }
  }
#endif
  lv_label_set_long_mode(tlbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(tlbl, txt_w_used);
  if (txt_w_used > inner_w)
    inner_w = txt_w_used;
  lv_obj_set_pos(tlbl, 0, inner_y);
  lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
  bindRowActions(bubble, ring_idx, m, has_url);

  lv_obj_update_layout(bubble);
  lv_coord_t bh = lv_obj_get_height(bubble);
  // Width for x-alignment is computed analytically from the widest child (text/sender/meta),
  // NOT lv_obj_get_width(): right after a send the layout isn't settled and get_width reads wide,
  // which put the (actually narrow) outgoing bubble on the LEFT until the next rebuild.
  lv_coord_t bw = inner_w + 2 * kChatBubblePadH;
  if (bw > kBubbleMaxW)
    bw = kBubbleMaxW;
  const lv_coord_t x_pos = m.outgoing ? (kContentW - bw - kChatSideGutter) : kChatSideGutter;
  lv_obj_set_pos(bubble, x_pos, vp_y);
  if (out_jump_y && s_chat_jump_msg_idx >= 0 && ring_idx == s_chat_jump_msg_idx)
    *out_jump_y = vp_y;
  lv_obj_set_user_data(bubble, reinterpret_cast<void *>(static_cast<intptr_t>(logical_i)));
  return bh;
}

static lv_coord_t chatVirtCreateCompactRow(LvChatPanel *p, int logical_i, int ring_idx, lv_coord_t vp_y,
                                           lv_coord_t *out_jump_y) {
  if (!p || !host.ready())
    return 0;
  MessageTypes::UIMessage m;
  if (!host.messageAt(ring_idx, m))
    return 0;

  ChatBubbleDisplay d{};
  chatParseMessageDisplay(m, p->channel_mode, s_chat_virt.thread_is_room, d);
  const bool mentions_me =
      (p->channel_mode || s_chat_virt.thread_is_room) && !m.outgoing && host.mentionsMe(d.show_text);
  char line[640];
#if defined(HAS_TDECK_PRO)
  const bool epaper_channel = p->channel_mode;
  if (epaper_channel)
    chatBuildCompactPlainLine(m, p, d, line, sizeof(line));
  else
#endif
    chatBuildCompactLine(m, p, logical_i, d, line, sizeof(line));

  lv_obj_t *row = rowRoot(p, true);
  lv_label_set_recolor(row,
#if defined(HAS_TDECK_PRO)
                       !epaper_channel
#else
                       true
#endif
  );
  lv_obj_add_flag(row, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_text_font(row, chatMessageFont(), LV_PART_MAIN);
  lv_obj_set_style_text_color(row, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(row, s_chat_virt.content_w);
  lv_label_set_text(row, line);
  lv_obj_set_style_pad_hor(row, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(row, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 3, LV_PART_MAIN);
#if defined(HAS_TDECK_PRO)
  if (epaper_channel) {
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(row, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  } else
#endif
      if (mentions_me) {
    lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_MENTION_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  } else if ((logical_i & 1) == 0) {
    lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_RECV_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  } else lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  bindRowActions(row, ring_idx, m, false);
  lv_obj_set_pos(row, 0, vp_y);
  lv_obj_update_layout(row);
  const lv_coord_t rh = lv_obj_get_height(row);
  if (out_jump_y && s_chat_jump_msg_idx >= 0 && ring_idx == s_chat_jump_msg_idx)
    *out_jump_y = vp_y;
  lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<intptr_t>(logical_i)));
  return rh;
}

static lv_coord_t chatVirtCreateMessageRow(LvChatPanel *p, int logical_i, int ring_idx, lv_coord_t vp_y,
                                           lv_coord_t *out_jump_y) {
  if (s_chat_virt.compact_chat)
    return chatVirtCreateCompactRow(p, logical_i, ring_idx, vp_y, out_jump_y);
  return chatVirtCreateBubble(p, logical_i, ring_idx, vp_y, out_jump_y);
}

static void updateRowMetadata(RowView& view, const MessageTypes::UIMessage& message, LvChatPanel* panel) {
  if (view.compact || !view.meta) return;
  char text[48], fitted[48];
  uint32_t color;
  chatBuildBubbleMeta(message, panel->channel_mode, text, sizeof text, &color);
  chatFitLeadingEllipsis(text, view.metaWidth, fitted, sizeof fitted);
#if defined(HAS_TDECK_PRO)
  color = 0x000000;
#endif
  if (strcmp(lv_label_get_text(view.meta), fitted) != 0 ||
      lv_color_to32(lv_obj_get_style_text_color(view.meta, LV_PART_MAIN)) != lv_color_to32(lv_color_hex(color))) {
    lv_label_set_text(view.meta, fitted);
    lv_obj_set_style_text_color(view.meta, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_x(view.meta, view.innerWidth - chatTextWidth(fitted));
    ++work.metadataUpdates;
  }
  if (view.action) {
    view.action->retry = message.outgoing && message.deliv_state == MessageTypes::DELIV_FAILED;
    int a, b;
    view.action->url = !view.action->retry && chatUrlSpan(message.text, 0, &a, &b);
  }
}

// Viewport Y for a message top: layout-space px relative to virt_top (1:1). Smooth
// sub-message scroll even when LVGL scroll_y is compressed; spacing stays correct
// because offsets[] use real measured bubble heights.
static lv_coord_t chatVirtMsgViewportY(int logical_i, int32_t virt_top) {
  if (logical_i < 0 || logical_i >= s_chat_virt.n || !s_chat_virt.offsets)
    return 0;
  const int64_t vp = static_cast<int64_t>(s_chat_virt.offsets[logical_i]) - virt_top;
  if (vp < -32768)
    return -32768;
  if (vp > 32767)
    return 32767;
  return static_cast<lv_coord_t>(vp);
}

int32_t chatVirtMsgVirtBottom(int logical_i) {
  if (logical_i < 0 || logical_i >= s_chat_virt.n || !s_chat_virt.offsets)
    return 0;
  if (logical_i + 1 < s_chat_virt.n)
    return s_chat_virt.offsets[logical_i + 1];
  return s_chat_virt.virt_total_h;
}

static void chatVirtFindVisibleRange(LvChatPanel *p, lv_coord_t lv_scroll_y, lv_coord_t lv_view_h,
                                     int &out_i0, int &out_i1) {
  const int n = s_chat_virt.n;
  if (n <= 0) {
    out_i0 = out_i1 = 0;
    return;
  }

  // Layout offsets are sorted, including when LVGL compresses scroll coords.
  const int32_t top = chatVirtEffectiveVirtTop(p);
  const int32_t y0 = std::max<int32_t>(0, top - kChatVirtOverscanPx);
  const int32_t y1 = top + lv_view_h + kChatVirtOverscanPx;
  const int32_t *first = s_chat_virt.offsets;
  const int32_t *last = first + n;
  out_i0 = static_cast<int>(std::upper_bound(first, last, y0) - first) - 1;
  if (out_i0 < 0) out_i0 = 0;
  out_i1 = static_cast<int>(std::lower_bound(first, last, y1) - first) - 1;
  if (out_i1 < out_i0) out_i1 = out_i0;
  if (out_i1 >= n) out_i1 = n - 1;
}

static void chatVirtRenderWindow(LvChatPanel *p, lv_coord_t scroll_y, lv_coord_t *out_jump_y) {
  if (!p || !p->msgs || s_chat_virt.panel != p || s_chat_virt.n <= 0 || !s_chat_virt.offsets)
    return;
  if (out_jump_y)
    *out_jump_y = -1;

  const lv_coord_t view_h = chatVirtMsgsViewH(p);
#if TRACE_MESSAGE_SCROLL_ACTIVITY
  const int old_i0 = s_chat_virt.last_i0;
  const int old_i1 = s_chat_virt.last_i1;
#endif
  int i0 = 0, i1 = 0;
  chatVirtFindVisibleRange(p, scroll_y, view_h, i0, i1);

#if TRACE_MESSAGE_SCROLL_ACTIVITY
  if (old_i0 >= 0 && (old_i0 != i0 || old_i1 != i1))
    chatVirtLogScrollTransition(p, old_i0, old_i1, i0, i1);
#endif

#if CAP_KEYPAD_NAV
  // Preserve focus without letting lv_obj_del() remove a focused group member.
  // Rows are recreated, so remember them by logical index; header/composer
  // controls survive the rebuild and can be retained by pointer.
  int refocus_i = -1;
#if defined(TLORA_PAGER)
  // An explicit pending target from the Pager encoder edge clamp wins.
  refocus_i = focusRequest.index;
#elif defined(HAS_M9_KEYBOARD)
  refocus_i = focusRequest.index;
#endif
  lv_obj_t *stable_focus = nullptr;
  if (host.navigationGroup()) {
    lv_obj_t *foc = lv_group_get_focused(host.navigationGroup());
    if (foc && lv_obj_get_parent(foc) == p->msgs) {
      const intptr_t ud = reinterpret_cast<intptr_t>(lv_obj_get_user_data(foc));
      if (refocus_i < 0 && ud >= 0)
        refocus_i = (int)ud;
    } else if (foc && lv_obj_is_valid(foc)) {
      stable_focus = foc;
    }
  }
  const bool nav_detached = (i0 != s_chat_virt.last_i0 || i1 != s_chat_virt.last_i1) && host.detachNavigation();
#endif

  ++work.windowUpdates;
  chatVirtEnsureSpacer(p, s_chat_virt.lv_total_h);
  // Reserve the overlap before recycling anything. The roots stay alive even
  // while an input device holds a row, so materialisation can run during a drag.
  bool used[kRowSlots]{};
  RowView* selected[kRowSlots]{};
  const int end = min(i1, i0 + int(kRowSlots) - 1);
  for (int i = i0; i <= end; ++i) {
    MessageTypes::UIMessage message;
    if (!host.messageAt(s_chat_virt.msg_idx[i], message)) continue;
    for (unsigned r = 0; r < kRowSlots; ++r) {
      auto& view = rows[r];
      if (!used[r] && view.root && view.compact == s_chat_virt.compact_chat &&
          view.ring == s_chat_virt.msg_idx[i] && view.sequence == message.seq) {
        selected[i - i0] = &view; used[r] = true; break;
      }
    }
  }
  for (unsigned r = 0; r < kRowSlots; ++r)
    if (!used[r] && rows[r].root) lv_obj_add_flag(rows[r].root, LV_OBJ_FLAG_HIDDEN);
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(p->msgs); ++c) {
    auto* child = lv_obj_get_child(p->msgs, c);
    if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(child)) < 0 || child == s_chat_virt.divider)
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_chat_virt.divider_i >= 0 && s_chat_virt.divider_y >= 0 && s_chat_virt.divider_i >= i0 &&
      s_chat_virt.divider_i <= i1) {
    const int32_t virt_top = chatVirtEffectiveVirtTop(p);
    const lv_coord_t div_vp = static_cast<lv_coord_t>(s_chat_virt.divider_y - virt_top);
    chatVirtCreateDivider(p, div_vp);
  }

  const int32_t virt_top = chatVirtEffectiveVirtTop(p);
  for (int i = i0; i <= end; ++i) {
    if (s_chat_virt.day_sep_y && s_chat_virt.day_sep_y[i] >= 0) {
      const lv_coord_t sep_vp = static_cast<lv_coord_t>(s_chat_virt.day_sep_y[i] - virt_top);
      chatVirtCreateDaySeparator(p, i, sep_vp);
    }
    MessageTypes::UIMessage message;
    if (!host.messageAt(s_chat_virt.msg_idx[i], message)) continue;
    auto* view = selected[i - i0];
    if (!view) {
      for (unsigned r = 0; r < kRowSlots; ++r) {
        if (!used[r] && (!rows[r].root || rows[r].compact == s_chat_virt.compact_chat)) {
          view = &rows[r]; used[r] = true; break;
        }
      }
    }
    if (!view) continue;
    const uint32_t signature = contentSignature(message);
    if (!view->root || view->ring != s_chat_virt.msg_idx[i] || view->sequence != message.seq ||
        view->signature != signature || view->styleEpoch != rowStyleEpoch ||
        (view->compact && view->logical != i)) {
      buildingRow = view;
      chatVirtCreateMessageRow(p, i, s_chat_virt.msg_idx[i], chatVirtMsgViewportY(i, virt_top), out_jump_y);
      buildingRow = nullptr;
      view->ring = s_chat_virt.msg_idx[i]; view->sequence = message.seq;
      view->signature = signature; view->styleEpoch = rowStyleEpoch;
    } else {
      lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
      updateRowMetadata(*view, message, p);
    }
    if (lv_obj_get_parent(view->root) != p->msgs) lv_obj_set_parent(view->root, p->msgs);
    view->logical = i;
    lv_obj_set_user_data(view->root, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    if (out_jump_y && view->ring == s_chat_jump_msg_idx) *out_jump_y = chatVirtMsgViewportY(i, virt_top);
  }

  s_chat_virt.last_i0 = i0;
  s_chat_virt.last_i1 = end;
  chatVirtSyncBubblePositions(p);
  CHAT_SCROLL_TRACE_DO(chatVirtCheckStoreEdges(p));

#if CAP_KEYPAD_NAV
  // Re-aim focus at the recreated row for the captured logical index. A fast
  // Pager encoder or M9 d-pad target can briefly run past the materialized
  // window, so clamp it to the nearest fresh row and continue from there.
  if (refocus_i >= 0) {
    if (refocus_i < i0)
      refocus_i = i0;
    if (refocus_i > i1)
      refocus_i = i1;
    const uint32_t nch2 = lv_obj_get_child_cnt(p->msgs);
    for (uint32_t c = 0; c < nch2; c++) {
      lv_obj_t *row = lv_obj_get_child(p->msgs, c);
      if (!row || !lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE) || lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN))
        continue;
      if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)) == (intptr_t)refocus_i) {
        host.focusHint(row);
        break;
      }
    }
#if defined(TLORA_PAGER)
    focusRequest.index = -1; // consumed (whether or not the row was found)
#elif defined(HAS_M9_KEYBOARD)
    focusRequest.index = -1;
#endif
  } else if (stable_focus && lv_obj_is_valid(stable_focus)) {
    host.focusHint(stable_focus);
  }
  // The group was deliberately empty throughout destruction. Recollect once,
  // consume the focus hint, and land the final focus before LVGL paints.
  if (nav_detached) {
    host.navigationDirty();
    host.rebuildNavigation();
  }
#endif
}

// Scroll/reflow/materialise — must NOT run inside the LVGL render timer: that
// tick is immediately followed by _lv_disp_refr_timer walking the same tree.
// Deleting/recreating floating bubbles there (or even sync lv_obj_del) races
// layout_update_core → LoadProhibited.  lv_async_call runs after the refresh.
static void chatVirtRenderAsyncCb(void *) {
  s_chat_virt_render_async_busy = false;
  LvChatPanel *p = s_chat_virt_render_async_panel;
  s_chat_virt_render_async_panel = nullptr;
  if (!p || !p->detail_open || s_chat_virt.panel != p || s_chat_virt.n <= 0)
    return;
  chatVirtApplyPendingScroll(p);
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(p->msgs);
  const lv_coord_t view_h = chatVirtMsgsViewH(p);
  int i0 = 0, i1 = 0;
  chatVirtFindVisibleRange(p, scroll_y, view_h, i0, i1);
  if (!chatVirtNeedReflow(i0, i1)) {
    chatVirtSyncBubblePositions(p);
    host.updateJumpButtons(p);
    return;
  }
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] re_layout on scroll_end [%d..%d] was [%d..%d]\n", i0 + 1, i1 + 1,
                           s_chat_virt.last_i0 + 1, s_chat_virt.last_i1 + 1);
  CHAT_SCROLL_TRACE_DO(chatVirtLogTopAnchor("re_layout_before", p, scroll_y));
  chatVirtRenderWindow(p, scroll_y, nullptr);
  CHAT_SCROLL_TRACE_DO(chatVirtLogTopAnchor("re_layout_after", p, lv_obj_get_scroll_y(p->msgs)));
  host.updateJumpButtons(p);
}

static void chatVirtRenderTimerCb(lv_timer_t *t) {
  LvChatPanel *p = s_chat_virt_render_panel;
  if (!p || !p->detail_open || s_chat_virt.panel != p || s_chat_virt.n <= 0) {
    s_chat_virt_render_panel = nullptr;
    lv_timer_pause(t);
    return;
  }
  s_chat_virt_render_panel = nullptr;
  lv_timer_pause(t);
  if (s_chat_virt_render_async_busy)
    return;
  s_chat_virt_render_async_panel = p;
  s_chat_virt_render_async_busy = true;
  if (lv_async_call(chatVirtRenderAsyncCb, nullptr) != LV_RES_OK) {
    // OOM: the call never queued, so nothing will ever clear the busy flag —
    // without this the chat would stop re-rendering until reboot.
    s_chat_virt_render_async_busy = false;
    s_chat_virt_render_async_panel = nullptr;
  }
}

void chatVirtScheduleRender(LvChatPanel *p) {
  if (!p || !p->detail_open || s_chat_virt.panel != p || s_chat_virt.n <= 0)
    return;
  if (s_chat_virt_render_panel == p || s_chat_virt_render_async_busy) return;
  s_chat_virt_render_panel = p;
  if (!s_chat_virt_render_timer) {
    s_chat_virt_render_timer = lv_timer_create(chatVirtRenderTimerCb, 16, nullptr);
    if (!s_chat_virt_render_timer) {
      s_chat_virt_render_panel = nullptr;
      return;
    }
  } else {
    lv_timer_set_period(s_chat_virt_render_timer, 16);
    lv_timer_reset(s_chat_virt_render_timer);
    lv_timer_resume(s_chat_virt_render_timer);
  }
}

void chatVirtOnScrollEnd(LvChatPanel *p) {
  if (!p || !p->detail_open || s_chat_virt.panel != p || s_chat_virt.n <= 0)
    return;
#if TRACE_MESSAGE_SCROLL_ACTIVITY
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(p->msgs);
  chatVirtLogTopAnchor("scroll_end", p, scroll_y);
#endif
  if (chatVirtCompressCoords() && s_chat_virt.scroll_virt_valid) {
    const lv_coord_t y = lv_obj_get_scroll_y(p->msgs);
    if (y <= 0)
      s_chat_virt.scroll_virt_top = 0;
    else if (lv_obj_get_scroll_bottom(p->msgs) <= 0)
      s_chat_virt.scroll_virt_top = chatVirtMaxVirtTop(p);
    const lv_coord_t anchored = chatVirtVirtToLv(s_chat_virt.scroll_virt_top);
    s_chat_virt.scroll_lv_anchor = anchored;
    if (anchored != y)
      lv_obj_scroll_to_y(p->msgs, anchored, LV_ANIM_OFF);
  }
  chatVirtSyncBubblePositions(p);
  chatVirtScheduleRender(p);
}

static lv_coord_t chatVirtBottomScrollY(LvChatPanel *p) {
  if (!p || !p->msgs)
    return 0;
  lv_obj_update_layout(p->msgs);
  return chatVirtMaxScrollY(p);
}

void chatVirtApplyPendingScroll(LvChatPanel *p) {
  if (!p || !p->msgs || !s_chat_virt.pending_scroll)
    return;
  const bool to_bottom = s_chat_virt.pending_scroll_bottom;
  s_chat_virt.pending_scroll = false;
  s_chat_virt.pending_scroll_bottom = false;
  lv_coord_t y = to_bottom ? chatVirtBottomScrollY(p) : s_chat_virt.pending_scroll_y;
  if (y < 0)
    y = 0;
  lv_obj_scroll_to_y(p->msgs, y, LV_ANIM_OFF);
  lv_obj_update_layout(p->msgs);
  chatVirtRefreshScrollArea(p);
  if (to_bottom)
    chatVirtSyncScrollState(p, y, chatVirtMaxVirtTop(p));
  else
    chatVirtSyncScrollState(p, y);
  s_chat_virt.last_i0 = -1;
  s_chat_virt.last_i1 = -1;
}

void chatVirtQueueScroll(LvChatPanel *p, lv_coord_t target) {
  if (!p)
    return;
  s_chat_virt.pending_scroll = true;
  if (target == LV_COORD_MAX) {
    s_chat_virt.pending_scroll_bottom = true;
  } else {
    s_chat_virt.pending_scroll_bottom = false;
    s_chat_virt.pending_scroll_y = target;
  }
  chatVirtScheduleRender(p);
}

void chatVirtJumpToOldest(LvChatPanel *p) {
  if (!p || !p->msgs)
    return;
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] jump_to_oldest (n=%d virt_h=%d lv_h=%d)\n", s_chat_virt.n,
                           (int)s_chat_virt.virt_total_h, (int)s_chat_virt.lv_total_h);
  chatVirtResetInputForMsgs(p);
  chatVirtCancelRenderTimer();
  s_chat_virt.last_i0 = -1;
  s_chat_virt.last_i1 = -1;
  chatVirtQueueScroll(p, 0);
  CHAT_SCROLL_TRACE_DO(chatVirtLogTopAnchor("jump_to_oldest_after", p, lv_obj_get_scroll_y(p->msgs)));
  host.updateJumpButtons(p);
}

void chatVirtJumpToLatest(LvChatPanel *p) {
  if (!p || !p->msgs)
    return;
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] jump_to_latest (n=%d virt_h=%d lv_h=%d)\n", s_chat_virt.n,
                           (int)s_chat_virt.virt_total_h, (int)s_chat_virt.lv_total_h);
  chatVirtResetInputForMsgs(p);
  chatVirtCancelRenderTimer();
  CHAT_SCROLL_TRACE_PRINTF("[CHAT] jump_to_latest target scroll_y=%d scroll_bottom=%d\n",
                           (int)lv_obj_get_scroll_y(p->msgs), (int)lv_obj_get_scroll_bottom(p->msgs));
  s_chat_virt.last_i0 = -1;
  s_chat_virt.last_i1 = -1;
  chatVirtQueueScroll(p, LV_COORD_MAX);
  host.updateJumpButtons(p);
}

void refreshChatDetail(LvChatPanel &p) {
  if (!host.ready() || !p.msgs)
    return;
  watchMessages(p);
  const bool opening = s_chat_just_opened;
  const lv_coord_t prev_scroll_y = lv_obj_get_scroll_y(p.msgs);
  const bool was_at_bottom = lv_obj_get_scroll_bottom(p.msgs) <= 8;
  int anchorRing = -1;
  uint32_t anchorSequence = 0;
  int32_t anchorDelta = 0;
  if (!opening && s_chat_virt.panel == &p && s_chat_virt.offsets && s_chat_virt.n > 0) {
    const int32_t top = chatVirtEffectiveVirtTop(&p);
    const int index = chatVirtFindMsgAtVirtTop(top);
    anchorRing = s_chat_msg_idx[index];
    anchorSequence = heightCache ? heightCache[anchorRing].sequence : 0;
    anchorDelta = top - s_chat_virt.offsets[index];
  }

  if (!host.hasActiveThread() || host.activeThreadIsChannel() != p.channel_mode) {
    chatVirtResetToPlaceholder(p, "No thread selected.\n\nTap a chat to open it.");
    return;
  }
  if (p.detail_open)
    host.markActiveThreadRead();

  chatVirtEnsureMsgIdx();
  if (!s_chat_msg_idx || s_chat_msg_idx_cap <= 0) {
    chatVirtResetToPlaceholder(p, "Low memory");
    return;
  }

  const int n = host.activeMessages(s_chat_msg_idx, s_chat_msg_idx_cap);
#if TRACE_MESSAGE_SCROLL_ACTIVITY
  if (opening) {
    char tname[MessageTypes::MAX_THREAD_NAME + 1];
    chatVirtGetThreadName(tname, sizeof(tname));
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] open channel=\"%s\" stored_messages=%d\n", tname[0] ? tname : "?", n);
    s_chat_virt_at_store_top = false;
    s_chat_virt_at_store_bottom = false;
  }
#endif
  if (n <= 0) {
    chatVirtResetToPlaceholder(p, "No messages yet.\nSay hello!");
#if defined(HAS_M9_KEYBOARD)
    if (opening)
      host.focusComposer(&p);
#endif
    s_chat_just_opened = false;
    s_chat_jump_msg_idx = -1;
    return;
  }

  int divider_i = -1;
  if (s_unread_at_open > 0) {
    divider_i = n - static_cast<int>(s_unread_at_open);
    if (divider_i < 0)
      divider_i = 0;
    if (divider_i >= n)
      divider_i = -1;
  }

  const bool compact_chat = touchPrefsGetCompactChat();
  // Content-generation guard: at ring capacity a new message evicts the oldest,
  // so n stays CONSTANT while every logical index shifts one slot — and a thread
  // switch on an already-open panel can land on an equal count too. Comparing the
  // endpoint ring slots catches both (the ring never reorders interior slots
  // while the endpoints hold, and two threads cannot share a slot). Without this
  // the old offsets kept describing the previous content and every row rendered
  // the next message's text at the previous message's position.
  const bool ring_changed = (s_chat_virt.n > 0) && (s_chat_msg_idx[0] != s_chat_virt.first_ring ||
                                                    s_chat_msg_idx[n - 1] != s_chat_virt.last_ring);
  bool changedHeight = false;
  if (s_chat_virt.panel == &p && heightCache && s_chat_virt.offsets && !ring_changed && s_chat_virt.n == n) {
    // Compact delivery text can wrap; only changed cache entries are measured.
    // Bubble metadata has a fixed header, so inspect just the materialised rows.
    const int begin = compact_chat ? 0 : max(0, s_chat_virt.last_i0);
    const int end = compact_chat ? n - 1 : min(n - 1, s_chat_virt.last_i1);
    for (int i = begin; i <= end; ++i) {
      MessageTypes::UIMessage message;
      if (host.messageAt(s_chat_msg_idx[i], message)) {
        const auto& entry = heightCache[s_chat_msg_idx[i]];
        if (entry.sequence != message.seq || entry.signature != contentSignature(message)) {
          changedHeight = true; break;
        }
      }
    }
  }
  const bool need_layout = (s_chat_virt.panel != &p) || (s_chat_virt.n != n) || !s_chat_virt.offsets ||
                           ring_changed || changedHeight || s_chat_virt.compact_chat != compact_chat;
  const bool divider_rebuild = !need_layout && divider_i >= 0 && s_chat_virt.divider_y < 0;
  const bool changing_panel = s_chat_virt.panel != &p;
  if (opening || need_layout || divider_rebuild)
    chatVirtCancelRenderTimer();
  if (opening || changing_panel) {
    chatVirtPurgeMsgsChildrenSync(&p);
    s_chat_virt.spacer = nullptr;
    s_chat_virt.divider = nullptr;
    s_chat_virt.scroll_virt_valid = false;
  }
  if (need_layout) {
    if (opening || changing_panel || s_chat_virt.compact_chat != compact_chat)
      chatVirtResetInputForMsgs(&p);
    s_chat_virt.last_i0 = -1;
    s_chat_virt.last_i1 = -1;
    if (!chatVirtRebuildLayout(&p, n, divider_i)) {
      chatVirtResetToPlaceholder(p, "Low memory");
      return;
    }
  } else {
    s_chat_virt.divider_i = divider_i;
    if (divider_rebuild) {
      s_chat_virt.last_i0 = -1;
      s_chat_virt.last_i1 = -1;
      if (!chatVirtRebuildLayout(&p, n, divider_i)) {
        chatVirtResetToPlaceholder(p, "Low memory");
        return;
      }
    }
    // (An `else if (g_lv.dirty_timeline)` rescue used to sit here — it was dead:
    // UITask::loop clears the flag right after SCHEDULING the async refresh, so
    // by the time this code ran it always read false. The ring_changed term in
    // need_layout above covers the cases it was meant to catch.)
  }

  lv_coord_t scroll_target = prev_scroll_y;
  if ((need_layout || divider_rebuild) && !opening && !was_at_bottom && anchorRing >= 0) {
    for (int i = 0; i < n; ++i) {
      if (s_chat_msg_idx[i] != anchorRing) continue;
      MessageTypes::UIMessage message;
      if (host.messageAt(anchorRing, message) && (!anchorSequence || message.seq == anchorSequence)) {
        const int32_t top = max<int32_t>(0, s_chat_virt.offsets[i] + anchorDelta);
        chatVirtSyncScrollState(&p, chatVirtVirtToLv(top), top);
        scroll_target = chatVirtVirtToLv(top);
      }
      break;
    }
  }
  if (s_chat_virt.scroll_virt_valid && chatVirtCompressCoords() && !s_chat_just_opened && !was_at_bottom) {
    scroll_target = chatVirtVirtToLv(s_chat_virt.scroll_virt_top);
  }
  if (s_chat_just_opened) {
    if (s_chat_jump_msg_idx >= 0 && s_chat_virt.offsets) {
      for (int i = 0; i < n; ++i) {
        if (s_chat_msg_idx[i] == s_chat_jump_msg_idx) {
          const int32_t virt = (s_chat_virt.offsets[i] > 8) ? (s_chat_virt.offsets[i] - 8) : 0;
          scroll_target = chatVirtVirtToLv(virt);
          break;
        }
      }
    } else if (divider_i >= 0 && s_chat_virt.divider_y >= 0) {
      const int32_t virt = (s_chat_virt.divider_y > 8) ? (s_chat_virt.divider_y - 8) : 0;
      scroll_target = chatVirtVirtToLv(virt);
    } else {
      scroll_target = LV_COORD_MAX;
    }
  } else if (was_at_bottom) {
    scroll_target = LV_COORD_MAX;
  }

  if (opening || need_layout || divider_rebuild) {
    if (opening) chatVirtResetInputForMsgs(&p);
    chatVirtQueueScroll(&p, scroll_target);
  } else {
    // ACK/echo refresh: retain roots, scroll animation and active touch input.
    chatVirtRenderWindow(&p, prev_scroll_y, nullptr);
  }

#if TRACE_MESSAGE_SCROLL_ACTIVITY
  if (opening) {
    const lv_coord_t vh = lv_obj_get_height(p.msgs);
    const lv_coord_t sy = lv_obj_get_scroll_y(p.msgs);
    const lv_coord_t sb = lv_obj_get_scroll_bottom(p.msgs);
    const int32_t layout_h = s_chat_virt.virt_total_h;
    CHAT_SCROLL_TRACE_PRINTF("[CHAT] open scroll_y=%d scroll_bottom=%d view_h=%d layout_h=%d\n", (int)sy,
                             (int)sb, (int)vh, (int)layout_h);
    chatVirtLogVisibleRange(&p, s_chat_virt.last_i0, s_chat_virt.last_i1);
  }
#endif

  s_chat_just_opened = false;
  s_chat_jump_msg_idx = -1;
  host.updateJumpButtons(&p);
}

static void refreshChatDetailAsyncCb(void *) {
  const uint8_t m = s_chat_detail_async_mask;
  s_chat_detail_async_mask = 0;
  s_chat_detail_async_queued = false;
  if ((m & 1) && host.direct->detail_open)
    refreshChatDetail(*host.direct);
  if ((m & 2) && host.channel->detail_open)
    refreshChatDetail(*host.channel);
}

void refreshChatDetailAsync(LvChatPanel &p) {
  if (&p == host.direct)
    s_chat_detail_async_mask |= 1;
  else if (&p == host.channel)
    s_chat_detail_async_mask |= 2;
  else
    return;
  watchMessages(p);
  if (!s_chat_detail_async_queued)
    s_chat_detail_async_queued = lv_async_call(refreshChatDetailAsyncCb, nullptr) == LV_RES_OK;
}

struct MessageWatch {
  ChatPanel *panel = nullptr;
  lv_obj_t *object = nullptr;
};
static MessageWatch messageWatches[2];
static void messagesDeleted(lv_event_t *event) {
  auto *watch = static_cast<MessageWatch *>(lv_event_get_user_data(event));
  auto *panel = watch->panel;
  if (panel && lv_event_get_target(event) == panel->msgs) {
    closed(panel);
    panel->msgs = nullptr;
  }
  watch->object = nullptr;
}
static void watchMessages(ChatPanel &panel) {
  if (&panel != host.direct && &panel != host.channel)
    return;
  auto &watch = messageWatches[&panel == host.direct ? 0 : 1];
  if (watch.object == panel.msgs)
    return;
  if (watch.object)
    lv_obj_remove_event_cb(watch.object, messagesDeleted);
  if (watch.object)
    lv_obj_remove_event_cb(watch.object, chatBackgroundTap);
  if (s_chat_virt.panel == &panel)
    chatVirtReset(&panel);
  watch.panel = &panel;
  watch.object = panel.msgs;
  if (!panel.msgs)
    return;
  lv_obj_add_event_cb(panel.msgs, messagesDeleted, LV_EVENT_DELETE, &watch);
  lv_obj_add_event_cb(panel.msgs, chatBackgroundTap, LV_EVENT_CLICKED, &panel);
}
void closed(ChatPanel *panel) {
  if (!panel || s_last_tap_panel == panel) s_last_tap_panel = nullptr;
  if (!panel || panel == host.direct)
    s_chat_detail_async_mask &= ~1;
  if (!panel || panel == host.channel)
    s_chat_detail_async_mask &= ~2;
  if (!s_chat_detail_async_mask) {
    lv_async_call_cancel(refreshChatDetailAsyncCb, nullptr);
    s_chat_detail_async_queued = false;
  }
  if (!panel || !s_chat_virt.panel || s_chat_virt.panel == panel) {
    chatVirtReset(panel);
    s_unread_at_open = 0;
    s_chat_just_opened = false;
    s_chat_jump_msg_idx = -1;
    closeUrlMenu();
    closeUrlQr();
  }
}

Snapshot snapshot() {
  return {
      s_chat_virt.panel,     s_chat_virt.n,         s_chat_virt.last_i0,      s_chat_virt.last_i1,
      s_chat_virt.divider_i, s_chat_virt.divider_y, s_chat_virt.virt_total_h, s_chat_virt.offsets != nullptr};
}
FocusRequest requestedFocus() { return focusRequest; }
void requestFocus(int logicalIndex) { focusRequest = {logicalIndex, ui::platform::milliseconds()}; }
void invalidateRows() { ++rowStyleEpoch; s_chat_virt.last_i0 = s_chat_virt.last_i1 = -1; }
Metrics metrics() { return work; }
void resetMetrics() { work = {}; }
int32_t messageCenter(int index) {
  return s_chat_virt.offsets && index >= 0 && index < s_chat_virt.n
             ? (s_chat_virt.offsets[index] + chatVirtMsgVirtBottom(index)) / 2
             : -1;
}
void opened(uint16_t unread) {
  s_unread_at_open = unread;
  s_chat_just_opened = true;
}
void jumpOnOpen(int ringIndex) { s_chat_jump_msg_idx = ringIndex; }
bool urlMenuOpen() { return s_urlmenu_root != nullptr; }
bool urlQrOpen() { return s_urlqr_root != nullptr; }
void shutdown() {
  closed(nullptr);
  for (auto &watch : messageWatches) {
    if (watch.object)
      lv_obj_remove_event_cb(watch.object, messagesDeleted);
    watch = {};
  }
  if (s_chat_virt_render_timer) {
    lv_timer_del(s_chat_virt_render_timer);
    s_chat_virt_render_timer = nullptr;
  }
  if (s_chat_msg_idx) {
    ui::platform::release(s_chat_msg_idx);
    s_chat_msg_idx = nullptr;
    s_chat_msg_idx_cap = 0;
  }
  ui::platform::release(heightCache);
  heightCache = nullptr;
  closeUrlMenu();
  closeUrlQr();
  host = {};
}
void configure(const Host &value) {
  if (host.ready)
    shutdown();
  host = value;
}

} // namespace timeline
} // namespace screens
} // namespace ui
