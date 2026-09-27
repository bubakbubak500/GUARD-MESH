// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/QuickReplyPicker.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
namespace picker = ui::screens::quickReplyPicker;
const char *replies[3];
bool fix, replaceOnInsert;
double latitude, longitude;
int insertions;
lv_obj_t *replacement;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *lastRoot() { return lv_obj_get_child(lv_layer_top(), lv_obj_get_child_cnt(lv_layer_top()) - 1); }
lv_obj_t *button(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class)) {
    const auto mode = lv_label_get_long_mode(object);
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    const bool match = strstr(lv_label_get_text(object), text) != nullptr;
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(object, mode);
    if (match) {
      auto *parent = lv_obj_get_parent(object);
      while (parent && !lv_obj_check_type(parent, &lv_btn_class))
        parent = lv_obj_get_parent(parent);
      if (parent)
        return parent;
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = button(lv_obj_get_child(object, i), text))
      return found;
  return nullptr;
}
lv_obj_t *expect(const char *text) {
  lv_obj_update_layout(lastRoot());
  auto *found = button(lastRoot(), text);
  check(found, "Quick reply picker button missing");
  return found;
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
picker::Host host() {
  return {[] { return 24; },
          [](lv_obj_t **root) {
            if (*root)
              lv_obj_add_flag(*root, LV_OBJ_FLAG_HIDDEN);
            *root = nullptr;
          },
          [] { return 3; },
          [](int index, char *out, size_t capacity) { return snprintf(out, capacity, "%s", replies[index]); },
          [](double &lat, double &lon) {
            lat = latitude;
            lon = longitude;
            return fix;
          },
          [](lv_obj_t *target, const char *text) {
            ++insertions;
            lv_textarea_add_text(target, text);
            if (replaceOnInsert)
              picker::open(replacement);
          }};
}
} // namespace
void runQuickReplyPickerRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  const uint8_t language = i18nGetLang();
  i18nSetLang(LANG_EN);
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  replies[0] = "First";
  replies[1] = "Second";
  replies[2] = "";
  fix = false;
  latitude = 1;
  longitude = 2;
  insertions = 0;
  replaceOnInsert = false;
  replacement = second;
  picker::configure(host());
  lv_textarea_set_text(first, "ab");
  lv_textarea_set_cursor_pos(first, 1);
  picker::open(first);
  auto *displayed = expect("First");
  replies[0] = "Changed";
  click(displayed);
  check(!strcmp(lv_textarea_get_text(first), "aFirstb") && insertions == 1 && !picker::isOpen(),
        "Quick reply did not insert displayed snapshot at cursor");
  picker::open(first);
  auto *oldRoot = lastRoot();
  auto *oldButton = expect("Changed");
  picker::open(second);
  auto *newButton = expect("Second");
  click(oldButton);
  click(oldRoot);
  check(insertions == 1 && picker::isOpen(), "Old quick reply event affected replacement");
  lv_obj_del(oldRoot);
  click(newButton);
  check(!strcmp(lv_textarea_get_text(second), "Second"), "Quick reply target switched incorrectly");
  picker::open(second);
  auto *empty = expect("(empty)");
  click(empty);
  check(insertions == 2 && !picker::isOpen(), "Empty quick reply inserted content");
  picker::open(second);
  auto *noFix = expect("no GPS fix");
  check(lv_obj_has_state(noFix, LV_STATE_DISABLED), "No-fix GPS button enabled");
  click(noFix);
  check(insertions == 2, "Disabled GPS row inserted coordinates");
  fix = true;
  picker::open(second);
  auto *gps = expect("1.00000");
  fix = false;
  click(gps);
  check(insertions == 2 && !picker::isOpen(), "Lost GPS fix inserted stale position");
  fix = true;
  picker::open(second);
  gps = expect("1.00000");
  latitude = 3.25;
  longitude = -4.5;
  click(gps);
  check(strstr(lv_textarea_get_text(second), "3.25000, -4.50000") && insertions == 3,
        "GPS position not refreshed at selection");
  picker::open(first);
  lv_obj_del(first);
  check(!picker::isOpen(), "Deleted target retained quick reply popup");
  picker::open(second);
  lv_obj_del(lastRoot());
  check(!picker::isOpen(), "Deleted quick reply root retained owner state");
  picker::open(second);
  picker::cancelFor(second);
  check(!picker::isOpen(), "Conversation change did not cancel quick reply");
  replaceOnInsert = true;
  picker::open(second);
  click(expect("Second"));
  check(picker::isOpen(), "Reentrant insertion closed replacement picker");
  replaceOnInsert = false;
  picker::close();
  while (lv_obj_get_child_cnt(lv_layer_top()) > roots)
    lv_obj_del(lastRoot());
  picker::configure({});
  i18nSetLang(language);
  check(!picker::isOpen() && lv_obj_get_child_cnt(lv_layer_top()) == roots,
        "Quick reply regression leaked roots");
}
