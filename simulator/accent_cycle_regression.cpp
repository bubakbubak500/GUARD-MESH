// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/AccentCyclePicker.h"
#include "widgets/ObjectRef.h"
#include "widgets/TextSelection.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace cycle = ui::screens::accentCycle;
bool replaceOnApply = false, replaceOnInsert = false;
lv_obj_t *replacement = nullptr;
int applied = 0, inserted = 0;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
void text(lv_obj_t *field, const char *value, int cursor = -1) {
  lv_textarea_set_text(field, value);
  lv_textarea_set_cursor_pos(field, cursor < 0 ? LV_TEXTAREA_CURSOR_LAST : cursor);
}
std::vector<lv_timer_t *> timers() {
  std::vector<lv_timer_t *> result;
  for (auto *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer))
    result.push_back(timer);
  return result;
}
bool live(lv_timer_t *timer) {
  auto current = timers();
  return std::find(current.begin(), current.end(), timer) != current.end();
}
lv_timer_t *addedTimer(const std::vector<lv_timer_t *> &before) {
  for (auto *timer : timers())
    if (timer->period == 900 && std::find(before.begin(), before.end(), timer) == before.end())
      return timer;
  throw std::runtime_error("Accent cycle did not own a timeout");
}
cycle::Host host() {
  return {[](lv_obj_t *field, uint32_t first, uint32_t last, const char *value) {
            ++applied;
            ui::widgets::textSelection::replace(field, first, last, value);
            if (replaceOnApply)
              cycle::altKey('r', replacement);
          },
          [](lv_obj_t *field, const char *value) {
            ++inserted;
            lv_textarea_add_text(field, value);
            if (replaceOnInsert)
              cycle::altKey('r', replacement);
          },
          [](lv_obj_t *popup, lv_obj_t *, bool) { lv_obj_set_pos(popup, 0, 24); }};
}
// Exercise real LVGL default insertion followed by the same handler order as UITask.
void afterKeyboard(lv_event_t *event) {
  auto *keyboard = lv_event_get_target(event);
  const auto selected = lv_btnmatrix_get_selected_btn(keyboard);
  cycle::afterKey(lv_keyboard_get_textarea(keyboard), lv_btnmatrix_get_btn_text(keyboard, selected));
}
void press(lv_obj_t *keyboard, const char *value) {
  const auto *map = lv_btnmatrix_get_map(keyboard);
  uint16_t button = 0;
  for (size_t i = 0; map[i] && map[i][0]; ++i) {
    if (!strcmp(map[i], "\n"))
      continue;
    if (!strcmp(map[i], value)) {
      lv_btnmatrix_set_selected_btn(keyboard, button);
      lv_event_send(keyboard, LV_EVENT_VALUE_CHANGED, nullptr);
      return;
    }
    ++button;
  }
  throw std::runtime_error("Accent cycle keyboard fixture key missing");
}
} // namespace
void runAccentCycleRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  auto *keyboard = lv_keyboard_create(fields);
  lv_keyboard_set_textarea(keyboard, first);
  lv_obj_add_event_cb(keyboard, afterKeyboard, LV_EVENT_VALUE_CHANGED, nullptr);
  replacement = second;
  applied = inserted = 0;
  replaceOnApply = replaceOnInsert = false;
  cycle::configure(host());
  text(first, "čau  X", 4);
  auto before = timers();
  check(cycle::longPress(first, "r"), "Long-press cycle failed to open");
  auto *timer = addedTimer(before);
  press(keyboard, "r");
  check(!strcmp(lv_textarea_get_text(first), "čau r X"), "First long-press insert changed base");
  press(keyboard, "r");
  check(!strcmp(lv_textarea_get_text(first), "čau ř X") && lv_textarea_get_cursor_pos(first) == 5,
        "Long-press cycle did not replace at real UTF-8 caret synchronously");
  press(keyboard, "r");
  check(!strcmp(lv_textarea_get_text(first), "čau r X"), "Long-press cycle failed to wrap");
  press(keyboard, "r");
  press(keyboard, "x");
  check(!cycle::isOpen() && !live(timer) && !strcmp(lv_textarea_get_text(first), "čau řx X"),
        "Other key lost text or left accent timeout");
  text(first, "a tail", 1);
  cycle::longPress(first, "r");
  press(keyboard, "r");
  lv_textarea_set_cursor_pos(first, 0);
  press(keyboard, "r");
  check(!cycle::isOpen() && !strcmp(lv_textarea_get_text(first), "rar tail"),
        "Long-press restored stale text after caret moved");
  text(first, "");
  cycle::longPress(first, "r");
  press(keyboard, "r");
  text(first, "edited");
  press(keyboard, "r");
  check(!cycle::isOpen() && !strcmp(lv_textarea_get_text(first), "editedr"),
        "Long-press restored stale text after external edit");
  const std::string longText = std::string(300, 'x') + " tail";
  text(first, longText.c_str(), 300);
  cycle::longPress(first, "r");
  press(keyboard, "r");
  press(keyboard, "r");
  check(std::string(lv_textarea_get_text(first)) == std::string(300, 'x') + "ř tail",
        "Long-press truncated prefix or suffix");
  cycle::close();
  text(first, "");
  before = timers();
  cycle::longPress(first, "r");
  timer = addedTimer(before);
  lv_timer_ready(timer);
  lv_timer_handler();
  check(!cycle::isOpen() && !live(timer), "Long-press timeout failed to release owner");
  before = timers();
  cycle::longPress(first, "r");
  timer = addedTimer(before);
  lv_obj_del(root());
  check(!cycle::isOpen() && !live(timer), "External cycle root DELETE left timeout");
  before = timers();
  cycle::longPress(first, "r");
  timer = addedTimer(before);
  text(second, "");
  cycle::afterKey(second, "r");
  check(!cycle::isOpen() && !live(timer), "Long-press accepted input from a new field");
  text(first, "ab", 1);
  check(!cycle::altKey('x', first) && !cycle::isOpen(), "ALT swallowed non-accent without active pick");
  check(cycle::altKey('r', first) && cycle::altActive(), "ALT cycle failed to open");
  check(cycle::altKey('x'), "ALT cycle failed to swallow stray key");
  cycle::altReleased();
  check(!strcmp(lv_textarea_get_text(first), "ařb") && !cycle::isOpen(),
        "ALT release inserted wrong glyph/caret");
  text(first, "");
  cycle::altKey('r', first);
  cycle::altKey('r');
  cycle::altReleased();
  check(!strcmp(lv_textarea_get_text(first), "r"), "ALT did not put plain base last");
  text(first, "");
  text(second, "");
  cycle::altKey('r', first);
  cycle::altKey('r', second);
  cycle::altReleased();
  check(!strcmp(lv_textarea_get_text(first), "") && !strcmp(lv_textarea_get_text(second), "ř"),
        "ALT same key retained previous field");
  text(first, "ab", 1);
  cycle::altKey('r', first);
  lv_textarea_set_cursor_pos(first, 0);
  const int beforeInsert = inserted;
  cycle::altReleased();
  check(inserted == beforeInsert && !cycle::isOpen(), "ALT released into a changed cursor");
  cycle::altKey('r', first);
  text(first, "changed");
  cycle::altReleased();
  check(inserted == beforeInsert && !cycle::isOpen(), "ALT released into changed text");
  cycle::altKey('r', first);
  cycle::cancelFor(second);
  cycle::cancelFor(nullptr);
  check(cycle::isOpen(), "Accent cycle cancelled for unrelated field");
  cycle::cancelFor(first);
  check(!cycle::isOpen(), "Accent cycle did not cancel for target");
  // DELETE on the popup can invalidate the destination before insertion.
  cycle::altKey('r', first);
  lv_keyboard_set_textarea(keyboard, nullptr);
  lv_obj_add_event_cb(
      root(), [](lv_event_t *e) { lv_obj_del(static_cast<lv_obj_t *>(lv_event_get_user_data(e))); },
      LV_EVENT_DELETE, first);
  cycle::altReleased();
  check(inserted == beforeInsert && !cycle::isOpen(), "ALT wrote into destination deleted during close");
  first = lv_textarea_create(fields);
  lv_keyboard_set_textarea(keyboard, first);
  text(first, "");
  text(second, "");
  replaceOnApply = true;
  cycle::longPress(first, "r");
  press(keyboard, "r");
  press(keyboard, "r");
  check(cycle::altActive(), "Cycle apply closed reentrant ALT replacement");
  replaceOnApply = false;
  cycle::altReleased();
  check(!strcmp(lv_textarea_get_text(second), "ř"), "Replacement ALT lost its destination");
  text(first, "");
  text(second, "");
  replaceOnInsert = true;
  cycle::altKey('r', first);
  cycle::altReleased();
  check(cycle::altActive(), "ALT release closed reentrant replacement");
  replaceOnInsert = false;
  cycle::altReleased();
  cycle::altKey('r', first);
  lv_keyboard_set_textarea(keyboard, nullptr);
  lv_obj_del(first);
  check(!cycle::isOpen() && !cycle::altActive(), "Cycle target DELETE left stale ALT state");
  cycle::configure({});
  lv_obj_del(fields);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Accent cycle leaked popup");
  std::puts("Accent cycles: real keyboard ordering, UTF-8/caret, snapshots, ALT, timeout, DELETE and "
            "reentrancy passed.");
}
