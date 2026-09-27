// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/MentionPicker.h"
#include "widgets/ObjectRef.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
namespace picker = ui::screens::mentionPicker;
const char *suggestions[3] = {"Alice", "Alena", "Bob"};
int applied = 0;
lv_obj_t *replacement = nullptr;
bool replaceOnApply = false;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
lv_obj_t *row(int index = 0) { return lv_obj_get_child(root(), index); }
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
void text(lv_obj_t *field, const char *value, int cursor = -1) {
  lv_textarea_set_text(field, value);
  lv_textarea_set_cursor_pos(field, cursor < 0 ? LV_TEXTAREA_CURSOR_LAST : cursor);
}
picker::Host host(bool navigation = true) {
  return {[](char out[][picker::NameBytes], int capacity) {
            for (int i = 0; i < 3 && i < capacity; ++i)
              snprintf(out[i], picker::NameBytes, "%s", suggestions[i]);
            return 3;
          },
          [](lv_obj_t *field, const char *value, uint32_t cursor) {
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
          },
          navigation};
}
} // namespace
void runMentionPickerRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  auto *detached = lv_obj_create(fields);
  replacement = second;
  applied = 0;
  replaceOnApply = false;
  picker::configure(host());
  for (const char *value : {"plain", "mail@al", "@@al", "@al,", "x(@al"}) {
    text(first, value);
    check(!picker::show(first), "Mention parser accepted email/punctuation/non-token");
  }
  text(first, "čau @alxyz, dál", 7); // caret after @al; replace the whole token, preserve suffix
  check(picker::show(first) && lv_obj_get_child_cnt(root()) == 2, "Mention prefix filtering failed");
  suggestions[0] = "Changed";
  click(row());
  check(!strcmp(lv_textarea_get_text(first), "čau @Alice, dál") && lv_textarea_get_cursor_pos(first) == 10,
        "Mention lost UTF-8 cursor/suffix or displayed name snapshot");
  suggestions[0] = "Alice";
  text(first, "@");
  check(picker::show(first), "Mention bare @ missing");
  picker::move(INT_MIN); // bounded modulo, no loop per encoder tick
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(first), "@Alena "), "Mention navigation wrap or trailing space wrong");
  text(first, "@al @bo", 3);
  picker::show(first);
  auto *old = row();
  lv_textarea_set_cursor_pos(first, 7);
  const int before = applied;
  click(old);
  check(applied == before && !picker::isOpen(), "Mention action followed caret to a different token");
  text(first, "@al");
  picker::show(first);
  text(first, "@bo");
  click(row());
  check(applied == before && !strcmp(lv_textarea_get_text(first), "@bo"),
        "Mention action ignored changed text");
  text(first, "@al");
  picker::show(first);
  old = row();
  lv_obj_set_parent(old, detached);
  text(second, "@bo");
  picker::show(second);
  click(old);
  check(applied == before && picker::isOpen(), "Detached old mention row changed replacement");
  lv_obj_del(old);
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(second), "@Bob "), "Replacement mention row did not apply");
  text(first, "@");
  picker::show(first);
  lv_obj_del(row());
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(first), "@Alena "), "Deleted mention row shifted suggestion identity");
  text(first, "@");
  picker::show(first);
  lv_obj_del(root());
  check(!picker::isOpen() && !picker::navigationActive(), "External mention root DELETE left state");
  picker::show(first);
  lv_obj_del(first);
  check(!picker::isOpen(), "Mention target DELETE left popup");
  first = lv_textarea_create(fields);
  text(first, "@al");
  picker::show(first);
  picker::cancelFor(nullptr);
  picker::cancelFor(second);
  check(picker::isOpen(), "Mention cancellation targeted unrelated field");
  picker::cancelFor(first);
  check(!picker::isOpen(), "Mention target cancellation failed");
  const std::string nearLimit = std::string(156, 'x') + " @a";
  text(first, nearLimit.c_str());
  picker::show(first);
  const int beforeLimit = applied;
  picker::confirm();
  check(applied == beforeLimit && !strcmp(lv_textarea_get_text(first), nearLimit.c_str()),
        "Mention overflow changed text");
  text(first, "@al");
  text(second, "@bo");
  picker::show(first);
  replaceOnApply = true;
  picker::confirm();
  check(picker::isOpen(), "Mention completion closed reentrant replacement");
  replaceOnApply = false;
  picker::confirm();
  check(!strcmp(lv_textarea_get_text(second), "@Bob "), "Reentrant mention lost target");
  picker::configure(host(false));
  text(first, "@al");
  picker::show(first);
  check(!picker::navigationActive(), "Touch-only mention picker captured navigation");
  picker::confirm();
  check(picker::isOpen(), "Touch-only mention reacted to hardware confirm");
  click(row());
  check(!picker::isOpen(), "Touch-only mention click failed");
  picker::configure({});
  lv_obj_del(fields);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Mention picker leaked root");
  std::puts("Mention picker: UTF-8/token snapshots, navigation, stale rows, DELETE and reentrancy passed.");
}
