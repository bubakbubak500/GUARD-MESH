// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/GlyphPicker.h"
#include <cstring>
#include <stdexcept>
namespace {
namespace picker = ui::screens::glyphPicker;
int insertions, returns, results;
uint32_t delivered;
bool replaceOnInsert;
lv_obj_t *replacement;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void closeRoot(lv_obj_t **root) {
  if (*root)
    lv_obj_add_flag(*root, LV_OBJ_FLAG_HIDDEN);
  *root = nullptr;
}
lv_obj_t *lastRoot() { return lv_obj_get_child(lv_layer_top(), lv_obj_get_child_cnt(lv_layer_top()) - 1); }
lv_obj_t *firstButton(lv_obj_t *object) {
  if (lv_obj_check_type(object, &lv_btn_class))
    return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = firstButton(lv_obj_get_child(object, i)))
      return found;
  return nullptr;
}
void click(lv_obj_t *object) {
  check(object, "Glyph button missing");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}
void result(uint32_t request, const char *text) {
  check(text && *text, "Empty picked glyph");
  ++results;
  delivered = request;
}
picker::Host host(bool selectFirst = false) {
  return {[] { return 24; },
          closeRoot,
          [](lv_obj_t *target, const char *text) {
            ++insertions;
            lv_textarea_add_text(target, text);
            if (replaceOnInsert)
              picker::open(replacement, picker::Set::Symbols);
          },
          [](lv_obj_t *target) {
            check(lv_obj_is_valid(target), "Picker returned focus to deleted field");
            ++returns;
          },
          selectFirst,
          selectFirst};
}
} // namespace
void runGlyphPickerRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  insertions = returns = results = 0;
  delivered = 0;
  replaceOnInsert = false;
  replacement = second;
  picker::configure(host());
  lv_textarea_set_text(first, "ab");
  lv_textarea_set_cursor_pos(first, 1);
  picker::open(first, picker::Set::Symbols);
  check(picker::activate() && picker::isOpen() && insertions == 0, "Unselected picker did not consume click");
  picker::move(1, 0);
  picker::move(1, 0);
  picker::move(1, 0);
  picker::activate();
  check(!strcmp(lv_textarea_get_text(first), "a%b") && insertions == 1 && !picker::isOpen(),
        "Picker motion threshold/cursor insertion changed");
  picker::open(first, picker::Set::Symbols);
  picker::move(0, 0);
  picker::move(picker::MotionStep, 0);
  picker::activate();
  check(strstr(lv_textarea_get_text(first), "$") && insertions == 2, "Discrete glyph navigation failed");
  picker::open(first);
  auto *oldRoot = lastRoot();
  auto *oldCell = firstButton(oldRoot);
  picker::open(second, picker::Set::Symbols);
  auto *currentCell = firstButton(lastRoot());
  click(oldCell);
  click(oldRoot);
  check(insertions == 2 && picker::isOpen(), "Old picker event affected replacement");
  lv_obj_del(oldRoot);
  check(picker::isOpen(), "Deleting old picker grid closed current picker");
  click(currentCell);
  check(insertions == 3 && !strcmp(lv_textarea_get_text(second), "%"), "Replacement target insertion failed");
  picker::open(first);
  lv_obj_del(first);
  check(!picker::isOpen(), "Deleted insertion target retained popup");
  picker::open(second);
  lv_obj_del(lastRoot());
  check(!picker::isOpen(), "External popup DELETE retained owner state");
  picker::open(second);
  auto *grid = lv_obj_get_parent(firstButton(lastRoot()));
  lv_obj_del(grid);
  check(!picker::isOpen(), "External grid DELETE retained picker");
  picker::pick(result, 41, "Pick icon");
  auto *oldResult = firstButton(lastRoot());
  picker::pick(result, 42, "Pick icon");
  auto *newResult = firstButton(lastRoot());
  click(oldResult);
  check(results == 0, "Old picker delivered new request id");
  click(newResult);
  check(results == 1 && delivered == 42, "Picker lost request identity");
  picker::pick(result, 43, "Pick icon");
  picker::cancelFor(nullptr);
  check(picker::isOpen(), "Null field cancelled result mode");
  picker::cancel(result);
  check(!picker::isOpen(), "Result cancellation failed");
  picker::open(second);
  picker::cancelFor(second);
  check(!picker::isOpen(), "Composer context cancellation failed");
  replaceOnInsert = true;
  picker::open(second, picker::Set::Symbols);
  click(firstButton(lastRoot()));
  check(picker::isOpen(), "Insertion callback's replacement popup was closed");
  replaceOnInsert = false;
  picker::configure(host(true));
  picker::open(second, picker::Set::Symbols);
  check(lv_obj_has_flag(lastRoot(), LV_OBJ_FLAG_USER_1), "Private keyboard navigation flag missing");
  picker::activate();
  check(!picker::isOpen(), "Preselected keyboard picker did not activate");
  picker::open(second, picker::Set::Symbols);
  auto *firstCell = firstButton(lastRoot());
  lv_obj_del(firstCell);
  picker::activate();
  check(strstr(lv_textarea_get_text(second), "$"), "Deleted cell shifted selection to wrong glyph identity");
  picker::close();
  while (lv_obj_get_child_cnt(lv_layer_top()) > roots)
    lv_obj_del(lastRoot());
  picker::configure({});
  check(!picker::isOpen() && returns > 0 && lv_obj_get_child_cnt(lv_layer_top()) == roots,
        "Glyph picker regression leaked roots");
}
