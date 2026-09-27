// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccentCyclePicker.h"
#include "../models/AccentCharacters.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include <cstdio>
#include <cstring>
namespace ui {
namespace screens {
namespace accentCycle {
using namespace theme;
using widgets::ObjectRef;
enum class Mode { Idle, WaitingForBase, Cycling, Alt };
static Host host{};
static Mode mode = Mode::Idle;
static lv_obj_t *root = nullptr;
static lv_timer_t *timer = nullptr;
static ObjectRef target;
static const accents::AccentSet *variants = nullptr;
static char base[2] = {};
static int count = 0, index = 0;
static char *original = nullptr;
static uint32_t cursor = 0;
static size_t begin = 0, end = 0;
static constexpr uint32_t CommitMs = 900;
static constexpr auto SkipNavigation = LV_OBJ_FLAG_USER_1;
static void targetDeleted(lv_event_t *);
static const char *option(int i) {
  if (mode == Mode::Alt)
    return i == variants->n ? base : variants->v[i];
  return i == 0 ? base : variants->v[i - 1];
}
static void clearState() {
  if (timer) {
    lv_timer_del(timer);
    timer = nullptr;
  }
  if (target.get())
    lv_obj_remove_event_cb(target.get(), targetDeleted);
  target.set(nullptr);
  platform::release(original);
  original = nullptr;
  mode = Mode::Idle;
  variants = nullptr;
  count = index = 0;
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
static void timeout(lv_timer_t *active) {
  if (active == timer)
    close();
}
void configure(const Host &value) {
  close();
  host = value;
}
void cancelFor(lv_obj_t *field) {
  if (field && field == target.get())
    close();
}
bool isOpen() { return root != nullptr; }
bool altActive() { return mode == Mode::Alt; }
static void highlight() {
  if (!root)
    return;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *cell = lv_obj_get_child(root, i);
    const bool selected = int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(cell))) == index;
    lv_obj_set_style_bg_color(
        cell,
        lv_color_hex(selected ? colors().COLOR_ACCENT : themeRole(0x10202E, colors().COLOR_ACCENT_SURFACE)),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
  }
}
static void showPopup() {
  // SC() so the box matches the scaled UI on the Tanmatsu (no-op at scale 100).
  const int cw = SC(30), ch = SC(30), gap = SC(4), pad = SC(6);
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  // Passive display only — never a keyboard-nav focus target and never
  // clickable. Without this the nav collector steals focus onto the popup,
  // the focused-textarea lookup goes null and the ALT-accent cycle key stops
  // reaching its handler (issue #129 first test).
  lv_obj_add_flag(root, SkipNavigation);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(colors().COLOR_ACCENT_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, pad, LV_PART_MAIN);
  lv_obj_set_style_pad_column(root, gap, LV_PART_MAIN);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(root, count * cw + (count - 1) * gap + pad * 2, ch + pad * 2);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < count; ++i) {
    lv_obj_t *c = lv_obj_create(root);
    lv_obj_remove_style_all(c);
    lv_obj_add_flag(c, SkipNavigation);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, cw, ch);
    lv_obj_set_style_radius(c, 5, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, option(i));
    lv_obj_set_style_text_font(l, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
    lv_obj_set_user_data(c, reinterpret_cast<void *>(intptr_t(i)));
  }
  lv_obj_update_layout(root);
  if (host.position)
    host.position(root, target.get(), mode == Mode::Alt);
  highlight();
}
static bool snapshot() {
  const char *text = lv_textarea_get_text(target.get());
  const size_t length = strlen(text);
  original = static_cast<char *>(platform::allocate(length + 1, true));
  if (!original)
    return false;
  memcpy(original, text, length + 1);
  cursor = lv_textarea_get_cursor_pos(target.get());
  return true;
}
static bool start(lv_obj_t *field, const accents::AccentSet *set, Mode next) {
  ObjectRef requested;
  if (!field || !requested.set(field))
    return false;
  close();
  // A DELETE callback may destroy the requested field or open a replacement.
  if (!requested.get() || root || !target.set(requested.get()))
    return false;
  lv_obj_add_event_cb(target.get(), targetDeleted, LV_EVENT_DELETE, nullptr);
  variants = set;
  base[0] = set->key;
  base[1] = 0;
  mode = next;
  index = 0;
  count = set->n + 1;
  return true;
}
bool longPress(lv_obj_t *field, const char *key) {
  if (mode == Mode::WaitingForBase || mode == Mode::Cycling || !host.replace)
    return false;
  const auto *set = accents::lookup(key);
  if (!set || !start(field, set, Mode::WaitingForBase))
    return false;
  showPopup();
  timer = lv_timer_create(timeout, CommitMs, nullptr);
  if (!timer) {
    close();
    return false;
  }
  return true;
}
void afterKey(lv_obj_t *field, const char *key) {
  if (mode != Mode::WaitingForBase && mode != Mode::Cycling)
    return;
  if (field != target.get() || !key || strcmp(key, base)) {
    close();
    return;
  }
  const char *text = lv_textarea_get_text(field);
  const uint32_t position = lv_textarea_get_cursor_pos(field);
  if (mode == Mode::WaitingForBase) {
    uint32_t byte = 0, cp = 0;
    while (text[byte] && cp < position) {
      _lv_txt_encoded_next(text, &byte);
      ++cp;
    }
    if (!byte || cp != position || text[byte - 1] != base[0] || !snapshot()) {
      close();
      return;
    }
    begin = byte - 1;
    end = byte;
    mode = Mode::Cycling;
    lv_timer_reset(timer);
    return;
  }
  // The default keyboard handler inserted exactly one base after the managed
  // glyph. Anything else (caret move, selection, rejected input, other edit)
  // cancels instead of restoring a stale snapshot over the user's text.
  const char *previous = option(index);
  const size_t oldSize = strlen(previous), length = strlen(original);
  if (position != cursor + 1 || strlen(text) != length + oldSize || memcmp(text, original, begin) ||
      memcmp(text + begin, previous, oldSize) || text[begin + oldSize] != base[0] ||
      strcmp(text + begin + oldSize + 1, original + end)) {
    close();
    return;
  }
  const int next = (index + 1) % count;
  const char *replacement = option(next);
  index = next;
  highlight();
  lv_timer_reset(timer);
  // State is committed before the callback, which may delete the field, close,
  // or open another cycle. The editing owner preserves the field's native
  // constraints and guards the lifetime between its change callbacks.
  host.replace(field, cursor - 1, cursor + 1, replacement);
}
bool altKey(char key, lv_obj_t *field) {
  if (!field && altActive())
    field = target.get();
  if (!field || static_cast<unsigned char>(key) < 32 || !host.insert)
    return false;
  const char text[2] = {key, 0};
  const auto *set = accents::lookup(text);
  if (!set)
    return altActive(); // consume strays only while selecting
  if (altActive() && target.get() == field && base[0] == key) {
    index = (index + 1) % count;
    highlight();
    return true;
  }
  if (!start(field, set, Mode::Alt))
    return false;
  if (!snapshot()) {
    close();
    return false;
  }
  showPopup();
  return true;
}
void altReleased() {
  if (!altActive())
    return;
  auto *field = target.get();
  if (!field || lv_textarea_get_cursor_pos(field) != cursor ||
      strcmp(lv_textarea_get_text(field), original)) {
    close();
    return;
  }
  char choice[8];
  snprintf(choice, sizeof choice, "%s", option(index));
  ObjectRef destination;
  destination.set(field);
  const auto insert = host.insert;
  close();
  if (destination.get() && insert)
    insert(destination.get(), choice);
}
} // namespace accentCycle
} // namespace screens
} // namespace ui
