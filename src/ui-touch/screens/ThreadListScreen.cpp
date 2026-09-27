// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThreadListScreen.h"
#include "../emoji_data.h"
#include "../models/MessageTypes.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ChatText.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
namespace ui {
namespace screens {
using namespace ui::theme;
using ui::widgets::fmtClockHM;
static constexpr auto NAV_HMOVE_FLAG = LV_OBJ_FLAG_USER_2;
static constexpr auto NAV_THREAD_ROW_FLAG = LV_OBJ_FLAG_USER_3;
struct ThreadListScreen::Action {
  ThreadListScreen *owner = nullptr;
  Action *next = nullptr;
  Action *previous = nullptr;
  lv_obj_t *row = nullptr;
  int index = 0;
  uint32_t signature = 0;
  bool channel = false, compact = false, seen = false, needsRender = false, held = false, opened = false;
  char name[MessageTypes::MAX_THREAD_NAME + 1] = {};
};
ThreadListScreen::~ThreadListScreen() { detach(); }
void ThreadListScreen::detach() {
  for (Action *action = _actions; action; action = action->next)
    action->owner = nullptr;
  _actions = nullptr;
  _list.set(nullptr);
  _signature = 0;
}
void ThreadListScreen::deleted(lv_event_t *event) {
  auto *action = static_cast<Action *>(lv_event_get_user_data(event));
  if (action->owner) {
    if (action->previous)
      action->previous->next = action->next;
    else
      action->owner->_actions = action->next;
    if (action->next)
      action->next->previous = action->previous;
    action->owner->_signature = 0;
  }
  // DELETE is delivered to both registered callbacks; detach the general one
  // before releasing its context, independent of LVGL callback ordering.
  lv_obj_remove_event_cb_with_user_data(action->row, rowEvent, action);
  platform::release(action);
}
bool ThreadListScreen::current(const Action &action) const {
  if (!_list.get() || lv_obj_get_parent(action.row) != _list.get())
    return false;
  bool channel = false;
  uint16_t unread = 0;
  uint32_t timestamp = 0;
  char name[MessageTypes::MAX_THREAD_NAME + 1] = {};
  return _host.info(action.index, channel, unread, timestamp, name, sizeof name) &&
         channel == action.channel && !strcmp(name, action.name);
}
void ThreadListScreen::dispatch(Action *action, bool menu) {
  auto *owner = action->owner;
  if (!owner || !owner->current(*action))
    return;
  const int index = action->index;
  const bool channel = action->channel;
  if (menu) {
    action->opened = true;
    char name[sizeof action->name];
    memcpy(name, action->name, sizeof name);
    auto callback = owner->_host.actions;
    if (auto *input = lv_indev_get_act())
      lv_indev_wait_release(input);
    callback(index, name, channel);
  } else
    owner->_host.select(index, channel);
}
void ThreadListScreen::gearEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  lv_event_stop_bubbling(event);
  dispatch(static_cast<Action *>(lv_event_get_user_data(event)), true);
}
void ThreadListScreen::rowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE)
    return;
  auto *action = static_cast<Action *>(lv_event_get_user_data(event));
  auto *owner = action->owner;
  if (!owner)
    return;
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    action->held = true;
    action->opened = false;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    action->held = false;
  else if (code == LV_EVENT_CLICKED) {
    if (!action->opened)
      dispatch(action, false);
  } else if (owner->_host.holdMilliseconds) {
    if (code == LV_EVENT_PRESSING && action->held && !action->opened &&
        owner->_host.holdMilliseconds() >= 800 && (!owner->_host.swiping || !owner->_host.swiping()))
      dispatch(action, true);
  } else if (code == LV_EVENT_LONG_PRESSED && !action->opened)
    dispatch(action, true);
}
// Rebuild the thread list inside a tab.
// Compact last-message time for a chat-list row: HH:MM today, "DD Mon" earlier
// this year, "DD/MM/YY" older. Empty when there's no timestamp (ts == 0).
static void formatChatRowTime(char *buf, size_t cap, uint32_t ts) {
  if (!buf || cap < 1)
    return;
  buf[0] = '\0';
  // Blank for "no real timestamp": 0 (empty/imported channel with no messages),
  // or a pre-2020 value left by an unsynced/garbage RTC — showing a bogus
  // 1969/1970/1902 date is worse than showing nothing (Ricky Leong).
  if (ts < 1577836800UL)
    return; // 2020-01-01 UTC
  time_t t = (time_t)ts;
  time_t now = time(nullptr);
  struct tm tmv, tmn;
  if (!platform::localTime(t, tmv) || !platform::localTime(now, tmn))
    return;
  if (tmv.tm_year == tmn.tm_year && tmv.tm_yday == tmn.tm_yday)
    fmtClockHM(buf, cap, &tmv);
  else if (tmv.tm_year == tmn.tm_year)
    strftime(buf, cap, "%d %b", &tmv);
  else
    strftime(buf, cap, "%d/%m/%y", &tmv);
}

void ThreadListScreen::refresh(lv_obj_t *list, bool channel, bool combined) {
  if (_list.get() != list || _channel != channel || _combined != combined) {
    detach();
    if (!_list.set(list))
      return;
    _channel = channel;
    _combined = combined;
  }
  if (!_list.get())
    return;

  int idxs[MessageTypes::MAX_UI_THREADS];
  int count = 0;
  count = _host.indexes(_channel, _combined, idxs, MessageTypes::MAX_UI_THREADS);
  if (count < 0)
    count = 0;
  if (count > MessageTypes::MAX_UI_THREADS)
    count = MessageTypes::MAX_UI_THREADS;

  // Hash each rendered row separately so a changed unread count or preview only
  // replaces that row. The aggregate hash also notices row order and mode changes.
  const bool compact_rows =
      _host.compact(); // compact = today's dense contact-style rows; off = WhatsApp-style
  uint32_t sig = 2166136261u;
  auto mix = [&sig](uint32_t v) { sig = (sig ^ v) * 16777619u; };
  uint32_t rowSignatures[MessageTypes::MAX_UI_THREADS] = {};
  bool validRows[MessageTypes::MAX_UI_THREADS] = {};
  bool rowChannels[MessageTypes::MAX_UI_THREADS] = {};
  mix((uint32_t)count);
  mix(compact_rows ? 0xC0FFEEu : 1u);
  for (int i = 0; i < count; ++i) {
    bool ch = false;
    uint16_t unread = 0;
    uint32_t ts = 0;
    char nm[MessageTypes::MAX_THREAD_NAME + 1];
    if (!_host.info(idxs[i], ch, unread, ts, nm, sizeof(nm)))
      continue;
    validRows[i] = true;
    rowChannels[i] = ch;
    uint32_t rowSignature = 2166136261u;
    auto mixRow = [&rowSignature](uint32_t value) { rowSignature = (rowSignature ^ value) * 16777619u; };
    mixRow((uint32_t)idxs[i]);
    mixRow(unread);
    mixRow(ts);
    char renderedTime[16];
    formatChatRowTime(renderedTime, sizeof renderedTime, ts);
    for (const char *text = renderedTime; *text; ++text)
      mixRow((uint8_t)*text);
    if (!compact_rows) {
      char sender[MessageTypes::MAX_SENDER_NAME + 1] = {}, text[80] = {};
      bool outgoing = false;
      mixRow(_host.scale ? _host.scale() : 0);
      mixRow(_host.lastMessage(idxs[i], sender, sizeof sender, text, sizeof text, &outgoing));
      mixRow(outgoing);
      for (const char *value = sender; *value; ++value)
        mixRow((uint8_t)*value);
      for (const char *value = text; *value; ++value)
        mixRow((uint8_t)*value);
    }
    mixRow((ch ? 2u : 0u) | (_host.mention(idxs[i]) ? 1u : 0u));
    for (const char *s = nm; *s; ++s)
      mixRow((uint8_t)*s);
    if (ch && !compact_rows) { // avatar-emoji change must re-render the row
      char eb[20];
      if (_host.emoji(nm, eb, sizeof eb))
        for (const char *s2 = eb; *s2; ++s2)
          mixRow((uint8_t)*s2);
    }
    rowSignatures[i] = rowSignature;
    mix(rowSignature);
  }
  if (sig == _signature && lv_obj_get_child_cnt(_list.get()) > 0)
    return; // nothing changed

  const lv_coord_t saved_scroll = lv_obj_get_scroll_y(_list.get());
  if (count <= 0) {
    lv_indev_reset(nullptr, nullptr); // #27: abort a scroll-throw before freeing rows
    lv_obj_clean(_list.get());
    const char *empty =
        _combined ? "No channels or chats yet" : (_channel ? "No channels yet" : "No chats yet");
    lv_obj_t *l = lv_list_add_text(_list.get(), empty);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 20, LV_PART_MAIN);
    _signature = sig;
    return;
  }

  // A prior empty-state label has no Action. Remove it once when rows appear.
  if (!_actions && lv_obj_get_child_cnt(_list.get()) > 0) {
    lv_indev_reset(nullptr, nullptr);
    lv_obj_clean(_list.get());
  }
  Action *ordered[MessageTypes::MAX_UI_THREADS] = {};
  for (Action *action = _actions; action; action = action->next)
    action->seen = false;
  for (int i = 0; i < count; ++i) {
    if (!validRows[i])
      continue;
    for (Action *action = _actions; action; action = action->next) {
      if (action->seen || action->index != idxs[i] || action->channel != rowChannels[i])
        continue;
      action->seen = true;
      action->needsRender = action->compact != compact_rows || action->signature != rowSignatures[i];
      break;
    }
  }
  bool removing = false;
  for (Action *action = _actions; action; action = action->next)
    if (!action->seen) {
      removing = true;
      break;
    }
  bool repainting = false;
  for (Action *action = _actions; action; action = action->next)
    if (action->seen && action->needsRender) {
      repainting = true;
      break;
    }
  if (removing || repainting)
    lv_indev_reset(nullptr, nullptr); // Removed row/content may still own a scroll-throw.
  if (!_list.get())
    return;
  for (;;) {
    Action *stale = nullptr;
    for (Action *action = _actions; action; action = action->next)
      if (!action->seen) {
        stale = action;
        break;
      }
    if (!stale)
      break;
    lv_obj_del(stale->row); // DELETE unlinks and retires its callback context.
    if (!_list.get())
      return;
  }
  // A deletion callback can itself remove another row. Resolve retained handles
  // only after deletions finish, so ordered[] never points at retired Actions.
  for (int i = 0; i < count; ++i) {
    if (!validRows[i])
      continue;
    for (Action *action = _actions; action; action = action->next)
      if (action->seen && action->index == idxs[i] && action->channel == rowChannels[i]) {
        ordered[i] = action;
        break;
      }
  }

  for (int i = 0; i < count; ++i) {
    if (!validRows[i] || (ordered[i] && !ordered[i]->needsRender))
      continue;
    bool ch = false;
    uint16_t unread = 0;
    uint32_t ts = 0;
    char name[MessageTypes::MAX_THREAD_NAME + 1];
    if (!_host.info(idxs[i], ch, unread, ts, name, sizeof(name)))
      continue;

    char san_name[MessageTypes::MAX_THREAD_NAME + 8];
    _host.sanitize(&font14(), san_name, sizeof(san_name), name);

    auto *action = ordered[i];
    const bool repaint = action != nullptr;
    const bool modeChanged = repaint && action->compact != compact_rows;
    if (!action) {
      action = static_cast<Action *>(platform::allocate(sizeof(Action), true));
      if (!action) {
        _signature = 0;
        return;
      }
      new (action) Action{};
    }
    action->owner = this;
    action->index = idxs[i];
    action->channel = ch;
    action->compact = compact_rows;
    action->signature = rowSignatures[i];
    action->seen = true;
    snprintf(action->name, sizeof action->name, "%s", name);
    lv_obj_t *btn = repaint ? action->row : nullptr;
    if (repaint) {
      // Keep the row and its focused gear; only its rendered content is stale.
      for (int child = static_cast<int>(lv_obj_get_child_cnt(btn)) - 1; child >= 0; --child) {
        auto *object = lv_obj_get_child(btn, child);
        if (!lv_obj_has_flag(object, NAV_HMOVE_FLAG))
          lv_obj_del(object);
      }
      if (modeChanged) {
        lv_theme_apply(btn);
        lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        if (compact_rows)
          lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
      }
    }
    if (compact_rows) {
      // ---- Compact rows (the dense contact-style list) ----
      // Name only — the unread count is shown as a right-aligned badge below.
      // DM = single person; channel = group of people (renders via the person_font
      // splice on font14()). Replaces the old envelope / loop-arrow glyphs.
      const char *icon = ch ? TOUCH_SYM_GROUP : TOUCH_SYM_PERSON;
      if (!btn)
        btn = lv_list_add_btn(_list.get(), icon, san_name);
      else {
        // lv_list_add_btn creates an image and a growing label in this order.
        // Recreate those children under the retained row, ahead of its gear.
        auto *image = lv_img_create(btn);
        lv_img_set_src(image, icon);
        auto *label = lv_label_create(btn);
        lv_label_set_text(label, san_name);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
        lv_obj_move_to_index(image, 0);
        lv_obj_move_to_index(label, 1);
      }

      // Match the Contacts-tab row recipe exactly (Kaj: one list design across the
      // tabs, and the same 34 px height so more threads fit per screen): panel fill,
      // 1 px bottom hairline, square corners, 16 px type icon + 14 px name.
      lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED),
                                LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN);
      lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
      lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
      lv_obj_set_style_text_color(btn, lv_color_hex(unread > 0 ? colors().COLOR_ACCENT : colors().COLOR_TEXT),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_font(btn, &font14(), LV_PART_MAIN);
      // Contacts rows are a FIXED 34 px; mirror that instead of min-height + fat
      // vertical padding, and centre the icon/name on the row's cross axis.
      lv_obj_set_style_pad_ver(btn, 0, LV_PART_MAIN);
      lv_obj_set_style_min_height(btn, 34, LV_PART_MAIN);
      lv_obj_set_height(btn, 34);
      lv_obj_set_style_pad_left(btn, 8, LV_PART_MAIN); // contacts icon_x
      lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      // Type icon in the contacts style: 16 px glyph, muted — the name carries the
      // unread accent, the icon stays neutral like the person/antenna icons do.
      if (lv_obj_t *icl = lv_obj_get_child(btn, 0)) {
        lv_obj_set_style_text_font(icl, &font16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(icl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
      }

      // Per-row settings gear on the far right — opens the thread-settings sheet (same as a long-press
      // and the in-chat cog). Tapping it swallows the gesture so the row's CLICKED can't open the chat.
      const lv_coord_t gear_w = 28;
      {
        lv_obj_t *gear = nullptr;
        for (uint32_t child = 0; child < lv_obj_get_child_cnt(btn); ++child) {
          auto *candidate = lv_obj_get_child(btn, child);
          if (lv_obj_has_flag(candidate, NAV_HMOVE_FLAG)) {
            gear = candidate;
            break;
          }
        }
        if (!gear) {
          gear = lv_btn_create(btn);
          lv_obj_remove_style_all(gear);
          lv_obj_add_flag(gear, LV_OBJ_FLAG_IGNORE_LAYOUT);
          lv_obj_add_flag(gear, NAV_HMOVE_FLAG); // horizontal nav keeps the row focusable
          lv_obj_add_event_cb(gear, gearEvent, LV_EVENT_CLICKED, action);
          lv_obj_t *gl = lv_label_create(gear);
          lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
          lv_obj_set_style_text_font(gl, &font14(), LV_PART_MAIN);
          lv_obj_set_style_text_color(gl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
          lv_obj_center(gl);
        }
        lv_obj_set_size(gear, gear_w, 30);
        lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -2, 0);
      }
      // Last-message time, just left of the gear; the unread badge + @ sit to its left.
      const lv_coord_t time_x = (lv_coord_t)(-(10 + gear_w));
      char tbuf[16];
      formatChatRowTime(tbuf, sizeof(tbuf), ts);
      lv_coord_t time_w = 0;
      if (tbuf[0]) {
        lv_point_t tsz;
        lv_txt_get_size(&tsz, tbuf, &font12(), 0, 0, LV_COORD_MAX, 0);
        time_w = tsz.x;
        lv_obj_t *tlbl = lv_label_create(btn);
        lv_obj_add_flag(tlbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(tlbl, tbuf);
        lv_obj_set_style_text_font(tlbl, &font12(), LV_PART_MAIN);
        lv_obj_set_style_text_color(
            tlbl, lv_color_hex(unread > 0 ? colors().COLOR_ACCENT : colors().COLOR_SUB), LV_PART_MAIN);
        lv_obj_align(tlbl, LV_ALIGN_RIGHT_MID, time_x, 0);
      }
      // Right edge for the unread / mention badges — just left of the time.
      const lv_coord_t r_edge = (lv_coord_t)((time_w > 0) ? (time_x - time_w - 8) : time_x);

      // Make long names scroll horizontally instead of being clipped.
      // lv_list_add_btn creates: child[0]=icon label, child[1]=text label.
      lv_obj_t *text_lbl = lv_obj_get_child(btn, 1);
      if (text_lbl) {
        lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        // Leave room on the right for the gear + time + unread badge.
        lv_obj_set_width(text_lbl, lv_disp_get_hor_res(nullptr) - 116 - time_w - gear_w);
      }

      // Right-aligned unread badge (pill with the count), left of the time.
      if (unread > 0) {
        lv_obj_t *badge = lv_label_create(btn);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT); // float, not in the row flex
        char cnt[8];
        // Cap the badge like every chat app: `unread` is a per-thread accumulator that
        // keeps climbing while you're NOT viewing the thread (even as the 500-msg ring
        // evicts the old messages), so a busy channel legitimately hits the hundreds —
        // showing a raw "328" reads as a bug. 99+ is the expected, non-alarming cap.
        if (unread > 99)
          snprintf(cnt, sizeof cnt, "99+");
        else
          snprintf(cnt, sizeof cnt, "%u", (unsigned)unread);
        lv_label_set_text(badge, cnt);
        lv_obj_set_style_text_font(badge, &font12(), LV_PART_MAIN);
        lv_obj_set_style_text_color(badge, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(badge, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 9, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(badge, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(badge, 1, LV_PART_MAIN);
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, r_edge, 0);
      }

      // Blue "@" to the left of the count when an unread message here @mentions me.
      if (_host.mention(idxs[i])) {
        lv_obj_t *at = lv_label_create(btn);
        lv_obj_add_flag(at, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(at, "@");
        lv_obj_set_style_text_font(at, &font14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(at, lv_color_hex(colors().COLOR_MENTION), LV_PART_MAIN);
        lv_obj_align(at, LV_ALIGN_RIGHT_MID, (lv_coord_t)(unread > 0 ? r_edge - 38 : r_edge), 0);
      }
    } else {
      // ---- WhatsApp-style rows (default, compact chat OFF) ----
      // Round avatar in the thread's signature colour with a stable per-name emoji,
      // name + last-message preview stacked next to it, time top-right, unread
      // pill + @ below the time, gear on the far edge.
      if (!btn)
        btn = lv_list_add_btn(_list.get(), nullptr, nullptr);
      lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED),
                                LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN);
      lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
      lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
#if defined(TLORA_PAGER)
      // Small stays at 48 px. Medium/Large gain only the pixels their two live
      // line boxes require, so name + preview cannot overlap without returning
      // to the oversized rows that made the short Pager list cumbersome.
      const lv_coord_t threadTextH = lv_font_get_line_height(&font14()) + lv_font_get_line_height(&font12());
      const lv_coord_t kThreadRowH = LV_MAX((lv_coord_t)48, (lv_coord_t)(threadTextH + 2));
      static constexpr lv_coord_t kThreadAvatar = 34;
      const lv_font_t *rowMetaFont = _host.scale() ? &lv_font_montserrat_14 : &font12();
#else
      static constexpr lv_coord_t kThreadRowH = 56;
      static constexpr lv_coord_t kThreadAvatar = 40;
      const lv_font_t *rowMetaFont = &font12();
#endif
      lv_obj_set_style_min_height(btn, kThreadRowH, LV_PART_MAIN);
      lv_obj_set_height(btn, kThreadRowH);
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

      // Avatar: same FNV-1a hue family as the chat-bubble colours (see
      // usernameBubbleColors), lifted in value so the disc reads on the dark panel.
#if !defined(HAS_TDECK_PRO)
      uint32_t hh = 2166136261u;
      for (const char *s2 = name; *s2; ++s2) {
        hh ^= (uint8_t)*s2;
        hh *= 16777619u;
      }
#endif
      lv_obj_t *av = lv_obj_create(btn);
      lv_obj_remove_style_all(av);
      lv_obj_add_flag(av, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_clear_flag(av, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); // taps fall through to the row
      lv_obj_set_size(av, kThreadAvatar, kThreadAvatar);
      lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, LV_PART_MAIN);
#if defined(HAS_TDECK_PRO)
      // Every generated avatar hue falls below the e-paper luminance threshold,
      // as does the black initials/emoji ink. Use an outlined paper-white disc
      // so the glyph remains visible after RGB565 is reduced to one bit.
      lv_obj_set_style_bg_color(av, lv_color_white(), LV_PART_MAIN);
      lv_obj_set_style_border_color(av, lv_color_black(), LV_PART_MAIN);
      lv_obj_set_style_border_width(av, 2, LV_PART_MAIN);
      lv_obj_set_style_border_opa(av, LV_OPA_COVER, LV_PART_MAIN);
#else
      lv_obj_set_style_bg_color(av, lv_color_hsv_to_rgb((uint16_t)(hh % 360u), 55, 42), LV_PART_MAIN);
#endif
      lv_obj_set_style_bg_opa(av, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_align(av, LV_ALIGN_LEFT_MID, 8, 0);
      // Avatar content: a user-chosen emoji for channels (thread sheet -> Chat icon),
      // otherwise the first two letters of the name (a leading '#' skipped, ASCII
      // uppercased) — the classic initials avatar.
      const lv_img_dsc_t *eg = nullptr;
      char av_glyph[20] = "";
      if (ch && _host.emoji(name, av_glyph, sizeof av_glyph)) {
        uint32_t goff = 0;
        eg = emojiGlyphLookup(
            _lv_txt_encoded_next(av_glyph, &goff)); // ZWJ glyphs are keyed on their lead codepoint
      }
      if (eg) {
        lv_obj_t *im = lv_img_create(av);
        lv_img_set_src(im, eg);
        lv_img_set_zoom(im, 384); // 16 px baked glyph -> ~24 px in the 40 px disc
        lv_img_set_antialias(im, true);
        lv_obj_center(im);
      } else {
        char initials[12];
        int o = 0, glyphs = 0;
        const char *q = name;
        while (*q == '#' || *q == ' ')
          ++q;
        while (*q && glyphs < 2 && o < 8) {
          const uint8_t c = (uint8_t)*q;
          int len = 1;
          if (c >= 0xF0)
            len = 4;
          else if (c >= 0xE0)
            len = 3;
          else if (c >= 0xC0)
            len = 2;
          for (int b = 0; b < len && *q; ++b)
            initials[o++] = *q++;
          ++glyphs;
        }
        initials[o] = '\0';
        for (char *u = initials; *u; ++u)
          if ((uint8_t)*u < 0x80)
            *u = (char)toupper((unsigned char)*u);
        char av_txt[16];
        _host.sanitize(&font16(), av_txt, sizeof av_txt, initials[0] ? initials : "?");
        lv_obj_t *fl = lv_label_create(av);
        lv_label_set_text(fl, av_txt);
        lv_obj_set_style_text_font(fl, &font16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(fl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
        lv_obj_center(fl);
      }

      // Per-row settings gear on the far right (same behaviour as the compact rows).
      const lv_coord_t gear_w = 28;
      {
        lv_obj_t *gear = nullptr;
        for (uint32_t child = 0; child < lv_obj_get_child_cnt(btn); ++child) {
          auto *candidate = lv_obj_get_child(btn, child);
          if (lv_obj_has_flag(candidate, NAV_HMOVE_FLAG)) {
            gear = candidate;
            break;
          }
        }
        if (!gear) {
          gear = lv_btn_create(btn);
          lv_obj_remove_style_all(gear);
          lv_obj_add_flag(gear, LV_OBJ_FLAG_IGNORE_LAYOUT);
          lv_obj_add_flag(gear, NAV_HMOVE_FLAG);
          lv_obj_add_event_cb(gear, gearEvent, LV_EVENT_CLICKED, action);
          lv_obj_t *gl = lv_label_create(gear);
          lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
          lv_obj_set_style_text_font(gl, &font14(), LV_PART_MAIN);
          lv_obj_set_style_text_color(gl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
          lv_obj_center(gl);
        }
        lv_obj_set_size(gear, gear_w, kThreadRowH - 8);
        lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -2, 0);
      }

      // Time, top-right (left of the gear).
      const lv_coord_t time_x = (lv_coord_t)(-(10 + gear_w));
      char tbuf[16];
      formatChatRowTime(tbuf, sizeof(tbuf), ts);
      lv_coord_t time_w = 0;
      if (tbuf[0]) {
        lv_point_t tsz;
        lv_txt_get_size(&tsz, tbuf, rowMetaFont, 0, 0, LV_COORD_MAX, 0);
        time_w = tsz.x;
        lv_obj_t *tlbl = lv_label_create(btn);
        lv_obj_add_flag(tlbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(tlbl, tbuf);
        lv_obj_set_style_text_font(tlbl, rowMetaFont, LV_PART_MAIN);
        lv_obj_set_style_text_color(
            tlbl, lv_color_hex(unread > 0 ? colors().COLOR_ACCENT : colors().COLOR_SUB), LV_PART_MAIN);
        lv_obj_align(tlbl, LV_ALIGN_TOP_RIGHT, time_x, 8);
      }

      // Name (top line) + last-message preview (bottom line), right of the avatar.
      const lv_coord_t text_x = 8 + kThreadAvatar + 8;
      const lv_coord_t name_w = (lv_coord_t)(lv_disp_get_hor_res(nullptr) - text_x - gear_w - time_w - 24);
      lv_obj_t *nm2 = lv_label_create(btn);
      lv_obj_add_flag(nm2, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_label_set_text(nm2, san_name);
      lv_obj_set_style_text_font(nm2, &font14(), LV_PART_MAIN);
      lv_obj_set_style_text_color(nm2, lv_color_hex(unread > 0 ? colors().COLOR_ACCENT : colors().COLOR_TEXT),
                                  LV_PART_MAIN);
      lv_label_set_long_mode(nm2, LV_LABEL_LONG_DOT);
      // Fixed ONE-LINE height: with only a width, LONG_DOT lets a long name wrap to
      // a second line (never truncating) and it overlapped the preview underneath.
      lv_obj_set_size(nm2, name_w,
#if defined(TLORA_PAGER)
                      lv_font_get_line_height(&font14())
#else
                      18
#endif
      );
      lv_obj_align(nm2, LV_ALIGN_TOP_LEFT, text_x,
#if defined(TLORA_PAGER)
                   _host.scale() == 0 ? 3 : 1
#else
                   9
#endif
      );

      // Preview: "sender: text" for channels, "You: text" for own DMs, plain text otherwise.
      char psender[MessageTypes::MAX_SENDER_NAME + 1] = "";
      char ptext[80] = "";
      bool pout = false;
      char preview[120] = "";
      if (_host.lastMessage(idxs[i], psender, sizeof psender, ptext, sizeof ptext, &pout)) {
        char raw[112];
        if (ch && psender[0] && !pout)
          snprintf(raw, sizeof raw, "%s: %s", psender, ptext);
        else if (pout)
          snprintf(raw, sizeof raw, "%s: %s", "You", ptext);
        else
          snprintf(raw, sizeof raw, "%s", ptext);
        // Previews render in a plain 12 px label: strip newlines, replace unbaked
        // glyphs, and let LONG_DOT ellipsize the rest.
        for (char *q = raw; *q; ++q)
          if (*q == '\n' || *q == '\r')
            *q = ' ';
        _host.sanitize(&font12(), preview, sizeof preview, raw);
      }
      lv_obj_t *pv = lv_label_create(btn);
      lv_obj_add_flag(pv, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_label_set_text(pv, preview[0] ? preview : "");
      lv_obj_set_style_text_font(pv, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(pv, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
      lv_label_set_long_mode(pv, LV_LABEL_LONG_DOT);
      lv_obj_set_size(pv, (lv_coord_t)(lv_disp_get_hor_res(nullptr) - text_x - gear_w - 60),
#if defined(TLORA_PAGER)
                      lv_font_get_line_height(&font12())
#else
                      16
#endif
      ); // one line, ellipsized
      lv_obj_align(pv, LV_ALIGN_BOTTOM_LEFT, text_x,
#if defined(TLORA_PAGER)
                   _host.scale() == 0 ? -3 : -1
#else
                   -8
#endif
      );

      // Unread pill bottom-right (under the time), @ to its left on a mention.
      if (unread > 0) {
        lv_obj_t *badge = lv_label_create(btn);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
        char cnt[8];
        if (unread > 99)
          snprintf(cnt, sizeof cnt, "99+");
        else
          snprintf(cnt, sizeof cnt, "%u", (unsigned)unread);
        lv_label_set_text(badge, cnt);
        lv_obj_set_style_text_font(badge, rowMetaFont, LV_PART_MAIN);
        lv_obj_set_style_text_color(badge, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(badge, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 9, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(badge, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(badge, 1, LV_PART_MAIN);
        lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, time_x, -6);
      }
      if (_host.mention(idxs[i])) {
        lv_obj_t *at = lv_label_create(btn);
        lv_obj_add_flag(at, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(at, "@");
        lv_obj_set_style_text_font(at, &font14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(at, lv_color_hex(colors().COLOR_MENTION), LV_PART_MAIN);
        lv_obj_align(at, LV_ALIGN_BOTTOM_RIGHT, (lv_coord_t)(time_x - (unread > 0 ? 34 : 0)), -6);
      }
    }
    if (!btn) {
      if (!repaint)
        platform::release(action);
      _signature = 0;
      continue;
    }

    // lv_list_btn's default theme paints a solid primary fill for FOCUS_KEY.
    // Chat navigation uses the shared cursor ring instead, so preserve the
    // row's normal panel fill while moving through conversations.
    lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_transform_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);

    if (!repaint) {
      action->row = btn;
      action->next = _actions;
      if (_actions)
        _actions->previous = action;
      _actions = action;
    }
    lv_obj_add_flag(btn, NAV_THREAD_ROW_FLAG);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(idxs[i] + 1)));
    if (!repaint) {
      lv_obj_add_event_cb(btn, deleted, LV_EVENT_DELETE, action);
      lv_obj_add_event_cb(btn, rowEvent, LV_EVENT_ALL, action);
    }
    action->needsRender = false;
    ordered[i] = action;
  }
  // Reorder retained rows in place; their Action and callback identity survives.
  int target = 0;
  for (int i = 0; i < count; ++i)
    if (ordered[i])
      lv_obj_move_to_index(ordered[i]->row, target++);
  // Keep the viewport still while row content is repainted or reordered.
  lv_obj_update_layout(_list.get());
  lv_obj_scroll_to_y(_list.get(), saved_scroll, LV_ANIM_OFF);
  _signature = sig;
}

} // namespace screens
} // namespace ui
