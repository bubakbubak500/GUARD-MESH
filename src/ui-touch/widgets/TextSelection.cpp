// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextSelection.h"
#include "../platform/UiPlatform.h"
#include "ObjectRef.h"
#include <cstring>
#include <memory>
namespace ui {
namespace widgets {
namespace textSelection {
static ObjectRef remembered, tapped;
static uint32_t selectionStart = 0, selectionEnd = 0, lastClick = 0;
static bool clickPending = false;
static void changed(lv_event_t *);
static bool textarea(lv_obj_t *field) { return field && lv_obj_check_type(field, &lv_textarea_class); }
static void forget() {
  if (remembered.get())
    lv_obj_remove_event_cb(remembered.get(), changed);
  remembered.set(nullptr);
  selectionStart = selectionEnd = 0;
}
static void changed(lv_event_t *event) {
  if (lv_event_get_target(event) == remembered.get())
    clear(remembered.get());
}
void reset() {
  forget();
  tapped.set(nullptr);
  clickPending = false;
}
uint32_t cpToByte(const char *text, uint32_t position) {
  if (!text)
    return 0;
  uint32_t byte = 0;
  while (text[byte] && position--)
    _lv_txt_encoded_next(text, &byte);
  return byte;
}
uint32_t byteToCp(const char *text, uint32_t position) {
  if (!text)
    return 0;
  uint32_t byte = 0, count = 0;
  while (text[byte] && byte < position) {
    _lv_txt_encoded_next(text, &byte);
    ++count;
  }
  return count;
}
void clear(lv_obj_t *field) {
  if (!textarea(field))
    return;
  lv_textarea_clear_selection(field);
  auto *value = reinterpret_cast<lv_textarea_t *>(field);
  value->sel_start = value->sel_end = LV_DRAW_LABEL_NO_TXT_SEL;
  if (remembered.get() == field)
    forget();
}
void select(lv_obj_t *field, uint32_t first, uint32_t last) {
  if (!textarea(field))
    return;
  if (first > last) {
    const auto swap = first;
    first = last;
    last = swap;
  }
  const auto length = _lv_txt_get_encoded_length(lv_textarea_get_text(field));
  if (last > length)
    last = length;
  if (first >= last) {
    clear(field);
    return;
  }
  forget();
  lv_textarea_set_text_selection(field, true);
  auto *value = reinterpret_cast<lv_textarea_t *>(field);
  value->sel_start = first;
  value->sel_end = last;
  if (value->label) {
    lv_label_set_text_sel_start(value->label, first);
    lv_label_set_text_sel_end(value->label, last);
  }
  lv_obj_invalidate(field);
  if (remembered.set(field)) {
    selectionStart = first;
    selectionEnd = last;
    lv_obj_add_event_cb(field, changed, LV_EVENT_VALUE_CHANGED, nullptr);
  }
}
bool range(lv_obj_t *field, uint32_t *first, uint32_t *last) {
  if (!textarea(field) || !first || !last)
    return false;
  const auto *value = reinterpret_cast<const lv_textarea_t *>(field);
  auto a = value->sel_start, b = value->sel_end;
  if (a == b || a == LV_DRAW_LABEL_NO_TXT_SEL || b == LV_DRAW_LABEL_NO_TXT_SEL)
    return false;
  if (a > b) {
    const auto swap = a;
    a = b;
    b = swap;
  }
  if (b > _lv_txt_get_encoded_length(lv_textarea_get_text(field)))
    return false;
  *first = a;
  *last = b;
  return true;
}
void restore(lv_obj_t *field) {
  if (field && remembered.get() == field && selectionStart != selectionEnd)
    select(field, selectionStart, selectionEnd);
}
void selectWord(lv_obj_t *field) {
  if (!textarea(field))
    return;
  const char *text = lv_textarea_get_text(field);
  const uint32_t length = strlen(text);
  uint32_t first = cpToByte(text, lv_textarea_get_cursor_pos(field)), last = first;
  auto word = [](char c) { return c != ' ' && c != '\n' && c != '\t' && c != '\r'; };
  while (first > 0 && word(text[first - 1]))
    --first;
  while (last < length && word(text[last]))
    ++last;
  select(field, byteToCp(text, first), byteToCp(text, last));
}
bool erase(lv_obj_t *field, uint32_t first, uint32_t last) {
  return first < last && replace(field, first, last, "");
}
static bool replaceNative(lv_obj_t *field, uint32_t first, uint32_t last, const char *replacement,
                          StillCurrent current, void *context) {
  const char *text = lv_textarea_get_text(field);
  const size_t length = strlen(text), added = strlen(replacement);
  using Buffer = std::unique_ptr<char, decltype(&platform::release)>;
  Buffer expected(static_cast<char *>(platform::allocate(length + added + 1, true)), platform::release);
  Buffer input(static_cast<char *>(platform::allocate(added + 1, true)), platform::release);
  if (!expected || !input)
    return false;
  memcpy(expected.get(), text, length + 1);
  memcpy(input.get(), replacement, added + 1);
  ObjectRef destination;
  destination.set(field);
  auto matches = [&](uint32_t cursor) {
    return (!current || current(context)) && destination.get() &&
           lv_textarea_get_cursor_pos(destination.get()) == cursor &&
           !strcmp(lv_textarea_get_text(destination.get()), expected.get());
  };
  clear(field);
  lv_textarea_set_cursor_pos(field, last);
  if (!matches(last))
    return false;
  for (uint32_t cursor = last; cursor > first; --cursor) {
    _lv_txt_cut(expected.get(), cursor - 1, 1);
    lv_textarea_del_char(destination.get());
    if (!matches(cursor - 1))
      return false;
  }
  uint32_t byte = 0, cursor = first;
  while (input.get()[byte]) {
    const uint32_t start = byte;
    const uint32_t codepoint = _lv_txt_encoded_next(input.get(), &byte);
    const uint32_t bytes = byte - start;
    if (!bytes || bytes > 4)
      return false;
    char glyph[5] = {};
    memcpy(glyph, input.get() + start, bytes);
    _lv_txt_ins(expected.get(), cursor, glyph);
    lv_textarea_add_char(destination.get(), _lv_txt_unicode_to_encoded(codepoint));
    if (matches(cursor + 1)) {
      ++cursor;
      continue;
    }
    // Native rejection (length, accepted characters or one-line mode) must
    // preserve the suffix. A callback that made a different edit cancels us.
    _lv_txt_cut(expected.get(), cursor, 1);
    if (!matches(cursor))
      return false;
  }
  return true;
}
bool replace(lv_obj_t *field, uint32_t first, uint32_t last, const char *replacement, StillCurrent current,
             void *context) {
  if ((current && !current(context)) || !textarea(field) || first > last || !replacement)
    return false;
  const char *text = lv_textarea_get_text(field);
  const auto characters = _lv_txt_get_encoded_length(text);
  if (first > characters)
    return false;
  if (last > characters)
    last = characters;
  const size_t begin = cpToByte(text, first), end = cpToByte(text, last), length = strlen(text);
  const size_t added = strlen(replacement);
  if (first == last && !added)
    return false;
  if (added > SIZE_MAX - length - 1)
    return false;
  if (lv_textarea_get_max_length(field) || lv_textarea_get_accepted_chars(field) ||
      lv_textarea_get_password_mode(field) || lv_textarea_get_one_line(field))
    return replaceNative(field, first, last, replacement, current, context);
  const auto nextCursor = first + _lv_txt_get_encoded_length(replacement);
  char *result = static_cast<char *>(platform::allocate(length - (end - begin) + added + 1, true));
  if (!result)
    return false;
  memcpy(result, text, begin);
  memcpy(result + begin, replacement, added);
  memcpy(result + begin + added, text + end, length - end + 1);
  ObjectRef destination;
  destination.set(field);
  clear(field);
  lv_textarea_set_text(field, result);
  const bool unchanged = (!current || current(context)) && destination.get() &&
                         !strcmp(lv_textarea_get_text(destination.get()), result);
  platform::release(result);
  if (!unchanged)
    return false;
  lv_textarea_set_cursor_pos(destination.get(), nextCursor);
  return true;
}
void clicked(lv_obj_t *field, uint32_t milliseconds) {
  if (!textarea(field))
    return;
  const bool twice = clickPending && tapped.get() == field && milliseconds - lastClick < 350;
  clickPending = !twice;
  lastClick = milliseconds;
  tapped.set(field);
  if (twice)
    selectWord(field);
  else
    forget();
}
} // namespace textSelection
} // namespace widgets
} // namespace ui
