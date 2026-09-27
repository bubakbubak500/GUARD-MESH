// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/AccentCharacters.h"
#include "screens/AccentPicker.h"
#include "widgets/ObjectRef.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
namespace picker = ui::screens::accentPicker;
int applied = 0;
bool replaceOnApply = false;
lv_obj_t *replacement = nullptr;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
lv_obj_t *row(int i = 0) { return lv_obj_get_child(root(), i); }
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
void text(lv_obj_t *field, const char *value, int cursor = -1) {
  lv_textarea_set_text(field, value);
  lv_textarea_set_cursor_pos(field, cursor < 0 ? LV_TEXTAREA_CURSOR_LAST : cursor);
}
picker::Host host() {
  return {[](lv_obj_t *field, const char *value, uint32_t cursor) {
            ++applied;
            ui::widgets::ObjectRef watched;
            watched.set(field);
            lv_textarea_set_text(field, value);
            if (watched.get())
              lv_textarea_set_cursor_pos(watched.get(), cursor);
            if (replaceOnApply)
              picker::show(replacement);
          },
          [](lv_coord_t &top, lv_coord_t &bottom) {
            top = 24;
            bottom = 220;
          }};
}
} // namespace
void runAccentPickerRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  auto *detached = lv_obj_create(fields);
  applied = 0;
  replaceOnApply = false;
  replacement = second;
  picker::configure(host());
  check(ui::accents::lookup("r") && !strcmp(ui::accents::lookup("r")->v[0], "ř") &&
            ui::accents::lookup("a")->n == 9 && !ui::accents::lookup("ab") && !ui::accents::lookup(""),
        "Accent catalog changed or accepted invalid key");
  text(first, "čau X", 3); // before cursor is u, end of text is X
  check(picker::show(first) && lv_obj_get_child_cnt(root()) == 5, "Accent ignored actual UTF-8 caret");
  click(row(4));
  check(!strcmp(lv_textarea_get_text(first), "čaů X") && lv_textarea_get_cursor_pos(first) == 3,
        "Accent replacement lost UTF-8 prefix/suffix or cursor");
  text(first, "x a", 1);
  check(!picker::show(first), "Accent used last character instead of character before caret");
  text(first, "a", 0);
  check(!picker::show(first), "Accent appeared before first character");
  text(first, "ř");
  check(!picker::show(first), "Accent accepted multi-byte base");
  text(first, "r");
  picker::show(first);
  check(!picker::navigationActive(), "Accent started navigation before arming");
  picker::confirm();
  check(picker::isOpen(), "Unarmed accent captured confirm");
  picker::enterNavigation();
  picker::move(INT_MAX);
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(first), "ř"), "Accent navigation failed");
  text(first, "a");
  picker::show(first);
  lv_obj_del(row());
  picker::enterNavigation();
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(first), "á"), "Deleted accent cell shifted variant identity");
  text(first, "a");
  picker::show(first);
  auto *old = row();
  lv_obj_set_parent(old, detached);
  text(second, "r");
  picker::show(second);
  const int before = applied;
  click(old);
  check(picker::isOpen() && applied == before, "Detached accent row affected replacement");
  lv_obj_del(old);
  click(row());
  check(!strcmp(lv_textarea_get_text(second), "ř"), "Replacement accent targeted wrong field");
  text(first, "a r", 1);
  picker::show(first);
  lv_textarea_set_cursor_pos(first, 3);
  click(row());
  check(!strcmp(lv_textarea_get_text(first), "a r"), "Accent followed moved cursor");
  text(first, "a");
  picker::show(first);
  text(first, "r");
  click(row());
  check(!strcmp(lv_textarea_get_text(first), "r"), "Accent ignored changed text snapshot");
  text(first, "a");
  picker::show(first);
  lv_obj_del(root());
  check(!picker::isOpen() && !picker::navigationActive(), "Accent root DELETE left state");
  picker::show(first);
  lv_obj_del(first);
  check(!picker::isOpen(), "Accent target DELETE left popup");
  first = lv_textarea_create(fields);
  text(first, "a");
  picker::show(first);
  picker::cancelFor(nullptr);
  picker::cancelFor(second);
  check(picker::isOpen(), "Accent cancelled for unrelated field");
  picker::cancelFor(first);
  check(!picker::isOpen(), "Accent cancelFor failed");
  // Settings text can exceed the message byte limit; no fixed-size truncation.
  const std::string longText = std::string(300, 'x') + "r tail";
  text(first, longText.c_str(), 301);
  picker::show(first);
  click(row());
  check(std::string(lv_textarea_get_text(first)) == std::string(300, 'x') + "ř tail",
        "Accent truncated long settings field");
  text(first, "a");
  text(second, "r");
  picker::show(first);
  replaceOnApply = true;
  click(row());
  check(picker::isOpen(), "Accent completion closed reentrant replacement");
  replaceOnApply = false;
  click(row());
  check(!strcmp(lv_textarea_get_text(second), "ř"), "Reentrant accent lost target");
  picker::configure({});
  lv_obj_del(fields);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Accent picker leaked roots");
  std::puts("Accent picker: caret replacement, UTF-8, long fields, navigation, stale rows, DELETE and "
            "reentrancy passed.");
}
