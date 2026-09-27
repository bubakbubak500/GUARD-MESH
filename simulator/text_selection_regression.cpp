// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/TextSelection.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
namespace selection = ui::widgets::textSelection;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void text(lv_obj_t *field, const char *value, int cursor = 0) {
  lv_textarea_set_text(field, value);
  lv_textarea_set_cursor_pos(field, cursor);
}
bool highlighted(lv_obj_t *field) {
  auto *label = lv_textarea_get_label(field);
  return reinterpret_cast<lv_label_t *>(label)->sel_start != LV_DRAW_LABEL_NO_TXT_SEL;
}
void countChange(lv_event_t *event) { ++*static_cast<int *>(lv_event_get_user_data(event)); }
} // namespace
void runTextSelectionRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *owner = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(owner);
  auto *second = lv_textarea_create(owner);
  selection::reset();
  check(selection::cpToByte("Ař🙂 Z", 3) == 7 && selection::byteToCp("Ař🙂 Z", 7) == 3 &&
            selection::cpToByte(nullptr, 7) == 0,
        "Text selection UTF-8 offsets failed");
  text(first, "Ař🙂 Z");
  selection::select(first, 3, 1);
  uint32_t start = 0, end = 0;
  check(selection::range(first, &start, &end) && start == 1 && end == 3,
        "Text selection range ordering failed");
  lv_textarea_clear_selection(first); // LVGL press clears highlight before long-press
  check(!highlighted(first), "Text selection fixture failed to clear live highlight");
  selection::restore(first);
  check(highlighted(first), "Text selection did not restore remembered highlight");
  lv_textarea_clear_selection(first);
  text(first, "other"); // same codepoint length must still invalidate remembered selection
  selection::restore(first);
  check(!highlighted(first), "Text selection restored stale same-length text");
  check(!selection::range(first, &start, &end), "Text change retained an invisible selection range");
  selection::select(first, 0, 99);
  check(selection::range(first, &start, &end) && end == 5, "Text selection failed to clamp range");
  selection::select(first, 99, 100);
  check(!selection::range(first, &start, &end), "Out-of-range text selection remained active");
  text(first, "čau world", 2);
  selection::selectWord(first);
  check(selection::range(first, &start, &end) && start == 0 && end == 3, "UTF-8 word selection failed");
  selection::clear(first);
  selection::restore(first);
  check(!selection::range(first, &start, &end), "Clear retained remembered selection");
  text(first, "Ař🙂 Z");
  selection::select(first, 1, 3);
  int changes = 0;
  lv_obj_add_event_cb(first, countChange, LV_EVENT_VALUE_CHANGED, &changes);
  check(selection::erase(first, 1, 3) && !strcmp(lv_textarea_get_text(first), "A Z") &&
            lv_textarea_get_cursor_pos(first) == 1 && changes == 1,
        "Selection erase was not one UTF-8-safe edit");
  check(!selection::range(first, &start, &end), "Selection erase retained stale range");
  check(!selection::erase(first, 3, 1) && !selection::erase(first, 99, 100) && changes == 1,
        "Invalid selection erase modified text");
  lv_obj_remove_event_cb(first, countChange);
  text(first, "ABCD", 2);
  lv_textarea_set_max_length(first, 6);
  check(selection::replace(first, 2, 2, "ř🙂XYZ") && !strcmp(lv_textarea_get_text(first), "ABř🙂CD") &&
            lv_textarea_get_cursor_pos(first) == 4,
        "Constrained paste lost suffix or split UTF-8");
  text(first, "12", 1);
  lv_textarea_set_accepted_chars(first, "0123456789");
  check(selection::replace(first, 1, 1, "a3b4") && !strcmp(lv_textarea_get_text(first), "1342"),
        "Constrained paste bypassed accepted characters");
  lv_textarea_set_accepted_chars(first, nullptr);
  lv_textarea_set_max_length(first, 0);
  lv_textarea_set_one_line(first, true);
  text(first, "ab", 1);
  check(selection::replace(first, 1, 1, "X\nY") && !strcmp(lv_textarea_get_text(first), "aXYb"),
        "One-line paste accepted newline");
  lv_textarea_set_password_mode(first, true);
  lv_textarea_set_max_length(first, 6);
  text(first, "ab", 1);
  check(selection::replace(first, 1, 1, "123456") && !strcmp(lv_textarea_get_text(first), "a1234b"),
        "Password paste lost text or bypassed length limit");
  lv_textarea_set_password_mode(first, false);
  lv_textarea_set_one_line(first, false);
  lv_textarea_set_max_length(first, 0);
  text(first, "word next", 2);
  selection::clicked(first, 0);
  selection::clicked(first, 100);
  check(selection::range(first, &start, &end) && start == 0 && end == 4, "Double tap at clock zero failed");
  lv_textarea_clear_selection(first);
  selection::clicked(first, 150);
  selection::restore(first);
  check(!highlighted(first), "Third tap reused consumed double tap");
  selection::reset();
  selection::clicked(first, UINT32_MAX - 100);
  selection::clicked(first, 50);
  check(highlighted(first), "Double tap failed across clock wrap");
  selection::select(first, 0, 4);
  selection::select(second, 0, 1); // empty second clears only itself
  selection::reset();
  lv_textarea_clear_selection(first);
  selection::restore(first);
  check(!highlighted(first), "Reset left selection observer active");
  selection::clicked(first, 200);
  selection::select(first, 0, 4);
  lv_obj_del(first);
  first = lv_textarea_create(owner);
  text(first, "new", 1);
  selection::clicked(first, 250);
  check(!highlighted(first), "Reused textarea inherited deleted tap/selection state");
  // The single VALUE_CHANGED may destroy the field; no post-callback dereference.
  text(first, "delete");
  selection::select(first, 0, 3);
  lv_obj_add_event_cb(
      first, [](lv_event_t *e) { lv_obj_del(lv_event_get_target(e)); }, LV_EVENT_VALUE_CHANGED, nullptr);
  check(!selection::erase(first, 0, 3), "Selection erase reported a deleted destination as live");
  first = lv_textarea_create(owner);
  text(first, "before");
  lv_obj_add_event_cb(
      first,
      [](lv_event_t *e) {
        auto *field = lv_event_get_target(e);
        if (!strcmp(lv_textarea_get_text(field), "fore"))
          text(field, "newer", 2);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  check(!selection::erase(first, 0, 2) && !strcmp(lv_textarea_get_text(first), "newer") &&
            lv_textarea_get_cursor_pos(first) == 2,
        "Selection erase overwrote a reentrant edit cursor");
  auto *limited = lv_textarea_create(owner);
  lv_textarea_set_max_length(limited, 20);
  text(limited, "delete");
  lv_obj_add_event_cb(
      limited, [](lv_event_t *e) { lv_obj_del(lv_event_get_target(e)); }, LV_EVENT_VALUE_CHANGED, nullptr);
  check(!selection::erase(limited, 0, 4), "Restricted erase continued after target DELETE");
  selection::reset();
  lv_obj_del(owner);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Text selection leaked widgets");
  std::puts("Text selection: UTF-8, sticky invalidation, taps, one-change erase, DELETE and reentrant edits "
            "passed.");
}
