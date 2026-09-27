// SPDX-License-Identifier: GPL-3.0-or-later
#include "MentionPicker.h"
#include "../models/MessageTypes.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
#include <cstring>
#include <strings.h>
namespace ui {
namespace screens {
namespace mentionPicker {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef target;
static constexpr int Limit = 6;
static char names[Limit][NameBytes];
static char original[MessageTypes::MAX_MSG_TEXT + 1];
static uint32_t originalCursor = 0;
static int count = 0, selected = 0;
static constexpr auto SkipNavigation = LV_OBJ_FLAG_USER_1;
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
  count = selected = 0;
}
void close() {
  auto *old = root;
  root = nullptr;
  clearState();
  if (old)
    lv_obj_del(old);
}
static void targetDeleted(lv_event_t *) { close(); }
static void rootDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) == root) {
    root = nullptr;
    clearState();
  }
}
void configure(const Host &value) {
  close();
  host = value;
}
void cancelFor(lv_obj_t *object) {
  if (object && object == target.get())
    close();
}
bool isOpen() { return root != nullptr; }
bool navigationActive() { return isOpen() && host.navigation; }
static void restyle() {
  if (!root || !host.navigation)
    return;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    setSelectionGlow(lv_obj_get_child(root, i), int(i) == selected, LV_PART_MAIN);
}
void move(int delta) {
  if (!navigationActive())
    return;
  const int size = int(lv_obj_get_child_cnt(root));
  if (!size) {
    close();
    return;
  }
  selected = (selected % size + delta % size + size) % size;
  restyle();
}
void confirm() {
  if (!navigationActive())
    return;
  if (selected >= 0 && selected < int(lv_obj_get_child_cnt(root)))
    lv_event_send(lv_obj_get_child(root, selected), LV_EVENT_CLICKED, nullptr);
  else
    close();
}
static uint32_t cpToByte(const char *text, uint32_t cursor) {
  uint32_t i = 0;
  while (text[i] && cursor--)
    _lv_txt_encoded_next(text, &i);
  return i;
}
static uint32_t byteToCp(const char *text, uint32_t byte) {
  uint32_t i = 0, count = 0;
  while (text[i] && i < byte) {
    _lv_txt_encoded_next(text, &i);
    ++count;
  }
  return count;
}
static bool mentionWhitespace(unsigned char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }
static bool mentionPunctuation(unsigned char c) {
  switch (c) {
  case ',':
  case '.':
  case ';':
  case ':':
  case '!':
  case '?':
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '<':
  case '>':
    return true;
  default:
    return false;
  }
}

struct MentionToken {
  size_t at;
  size_t caret;
  size_t end;
};

// Locate the active @token around the actual caret. The start must be at text
// start or after whitespace, so an email address never opens the picker.
static bool mentionTokenAtCaret(lv_obj_t *ta, MentionToken &token) {
  if (!ta)
    return false;
  const char *text = lv_textarea_get_text(ta);
  if (!text)
    return false;
  const size_t len = strlen(text);
  size_t caret = cpToByte(text, lv_textarea_get_cursor_pos(ta));
  if (caret > len)
    caret = len;

  size_t at = caret;
  bool found = false;
  while (at > 0) {
    const unsigned char c = (unsigned char)text[at - 1];
    if (c == '@') {
      --at;
      found = true;
      break;
    }
    if (mentionWhitespace(c) || mentionPunctuation(c))
      return false;
    --at;
  }
  if (!found || (at > 0 && !mentionWhitespace((unsigned char)text[at - 1])))
    return false;

  size_t end = caret;
  while (end < len && !mentionWhitespace((unsigned char)text[end]) &&
         !mentionPunctuation((unsigned char)text[end]))
    ++end;
  token = {at, caret, end};
  return true;
}

static void cellClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !owns(event))
    return;
  const int index = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  auto *field = target.get();
  MentionToken token{};
  if (!field || index < 0 || index >= count || strcmp(lv_textarea_get_text(field), original) != 0 ||
      lv_textarea_get_cursor_pos(field) != originalCursor || !mentionTokenAtCaret(field, token)) {
    close();
    return;
  }
  const size_t length = strlen(names[index]), suffix = strlen(original + token.end);
  const bool space = original[token.end] == '\0';
  const size_t needed = token.at + 1 + length + (space ? 1 : 0) + suffix;
  if (needed > MessageTypes::MAX_MSG_TEXT) {
    close();
    return;
  }
  char text[MessageTypes::MAX_MSG_TEXT + 1];
  size_t pos = token.at;
  memcpy(text, original, pos);
  text[pos++] = '@';
  memcpy(text + pos, names[index], length);
  pos += length;
  if (space)
    text[pos++] = ' ';
  const size_t caret = pos;
  memcpy(text + pos, original + token.end, suffix + 1);
  const uint32_t cursor = byteToCp(text, caret);
  ObjectRef destination;
  destination.set(field);
  const auto apply = host.apply;
  close();
  if (destination.get() && apply)
    apply(destination.get(), text, cursor);
}
bool show(lv_obj_t *field) {
  close();
  MentionToken token{};
  if (!field || !host.names || !host.apply || !mentionTokenAtCaret(field, token))
    return false;
  const char *text = lv_textarea_get_text(field);
  if (strlen(text) > MessageTypes::MAX_MSG_TEXT)
    return false;
  const char *partial = text + token.at + 1;
  const size_t plen = token.caret - token.at - 1;
  char recent[SourceLimit][NameBytes] = {};
  int total = host.names(recent, SourceLimit);
  if (total > SourceLimit)
    total = SourceLimit;
  int n = 0;
  for (int i = 0; i < total && n < Limit; ++i) {
    recent[i][NameBytes - 1] = 0;
    if (!recent[i][0] || (plen && strncasecmp(recent[i], partial, plen) != 0))
      continue;
    memcpy(names[n++], recent[i], NameBytes);
  }
  if (!n || !target.set(field))
    return false;
  lv_obj_add_event_cb(field, targetDeleted, LV_EVENT_DELETE, nullptr);
  memcpy(original, text, strlen(text) + 1);
  originalCursor = lv_textarea_get_cursor_pos(field);
  selected = 0;
  count = n;
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_add_flag(root, SkipNavigation); // passive, touch-only — never a keyboard-nav stop (issue #42)
  lv_obj_remove_style_all(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(colors().COLOR_ACCENT_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_row(root, 3, LV_PART_MAIN);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  const int rowh = 30, boxw = 168;
  for (int i = 0; i < n; ++i) {
    lv_obj_t *b = lv_btn_create(root);
    lv_obj_add_flag(b, SkipNavigation);
    lv_obj_set_size(b, boxw, rowh);
    lv_obj_set_style_radius(b, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_ACCENT_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cellClicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(intptr_t(i)));
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_fmt(l, "@%s", names[i]);
    lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, boxw - 18);
    lv_obj_center(l);
  }
  lv_obj_set_size(root, boxw + 8, n * rowh + (n - 1) * 3 + 8);
  restyle();

  lv_obj_update_layout(root);
  lv_area_t area;
  lv_obj_get_coords(field, &area);
  const lv_coord_t width = lv_obj_get_width(root), height = lv_obj_get_height(root);
  lv_coord_t top = 0, bottom = lv_disp_get_ver_res(nullptr);
  if (host.bounds)
    host.bounds(top, bottom);
  lv_coord_t y = area.y1 - height - 4;
  if (y + height > bottom)
    y = bottom - height;
  if (y < top)
    y = top;
  lv_obj_set_pos(root, (lv_disp_get_hor_res(nullptr) - width) / 2, y);
  lv_obj_move_foreground(root);
  return true;
}
} // namespace mentionPicker
} // namespace screens
} // namespace ui
