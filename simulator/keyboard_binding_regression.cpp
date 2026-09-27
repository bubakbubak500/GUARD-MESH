// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/KeyboardBinding.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int readyCount = 0;
void ready(lv_event_t *) { ++readyCount; }
struct ChangeAction {
  ui::widgets::KeyboardBinding *binding;
  lv_obj_t *keyboard, *next;
  int mode, calls;
};
void changeAction(lv_event_t *event) {
  auto &action = *static_cast<ChangeAction *>(lv_event_get_user_data(event));
  if (++action.calls != 1)
    return;
  if (action.mode == 0)
    lv_obj_del(lv_event_get_target(event));
  else if (action.mode == 1)
    action.binding->clear();
  else
    check(action.binding->bind(action.keyboard, action.next, true), "Reentrant mirror rebind failed");
}
} // namespace
void runKeyboardBindingRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  lv_obj_t *page = lv_obj_create(lv_layer_top());
  lv_obj_t *keyboard = lv_keyboard_create(page);
  lv_obj_t *first = lv_textarea_create(page);
  lv_obj_t *second = lv_textarea_create(page);
  lv_textarea_set_text(first, "first");
  lv_textarea_set_text(second, "second");
  lv_obj_add_event_cb(first, ready, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(second, ready, LV_EVENT_READY, nullptr);
  {
    ui::widgets::KeyboardBinding binding;
    binding.ensureCreated(24, 52);
    check(binding.bind(keyboard, first, true), "Could not bind keyboard mirror");
    check(lv_keyboard_get_textarea(keyboard) == binding.mirror(), "Keyboard did not target mirror");
    lv_textarea_set_text(binding.mirror(), "edited first");
    check(!strcmp(lv_textarea_get_text(first), "edited first"), "Mirror did not update real field");
    binding.setText(first, "picked value");
    binding.sync();
    check(!strcmp(lv_textarea_get_text(first), "picked value") &&
              !strcmp(lv_textarea_get_text(binding.mirror()), "picked value"),
          "Programmatic edit overwritten by stale mirror");
    lv_textarea_set_max_length(second, 9);
    lv_textarea_set_password_mode(second, true);
    check(binding.bind(keyboard, second, true), "Could not switch mirror field");
    check(!strcmp(lv_textarea_get_text(second), "second"), "Mirror initialization overwrote next field");
    check(lv_textarea_get_password_mode(binding.mirror()) &&
              lv_textarea_get_max_length(binding.mirror()) == 9,
          "Mirror did not adopt field policy");
    lv_obj_del(first);
    first = nullptr;
    check(binding.target() == second, "Deleting previous field detached current binding");
    lv_event_send(binding.mirror(), LV_EVENT_READY, nullptr);
    check(readyCount == 1, "Mirror READY not delivered exactly once");
    lv_obj_del(second);
    second = nullptr;
    check(!binding.target() && !lv_keyboard_get_textarea(keyboard),
          "Deleted field left keyboard/binding dangling");
    binding.sync();
    lv_event_send(binding.mirror(), LV_EVENT_READY, nullptr);
    check(readyCount == 1, "READY delivered after field deletion");

    first = lv_textarea_create(page);
    lv_textarea_set_text(first, "abcd");
    check(binding.bind(keyboard, first, false), "Could not bind direct keyboard");
    lv_textarea_set_cursor_pos(first, 2);
    lv_textarea_del_char(lv_keyboard_get_textarea(keyboard));
    binding.sync();
    check(!strcmp(lv_textarea_get_text(first), "acd"),
          "Direct keyboard lost caret position or received stale mirror");
    check(binding.bind(keyboard, first, true), "Could not switch to mirrored input");
    lv_obj_del(binding.root());
    check(!binding.root() && !binding.mirror() && !binding.target() && !lv_keyboard_get_textarea(keyboard),
          "External mirror deletion left references");
    binding.ensureCreated(24, 52);
    check(binding.bind(keyboard, first, true), "Mirror could not be recreated");
    lv_obj_del(binding.mirror());
    check(!binding.mirror() && !binding.target(), "External mirror field deletion left binding");
    binding.ensureCreated(24, 52);
    check(binding.bind(keyboard, first, true), "Mirror child could not be recreated");

    // VALUE_CHANGED may close a form while sync is still on the stack.
    lv_obj_add_event_cb(
        first,
        [](lv_event_t *event) {
          auto *binding = static_cast<ui::widgets::KeyboardBinding *>(lv_event_get_user_data(event));
          binding->clear();
        },
        LV_EVENT_VALUE_CHANGED, &binding);
    lv_textarea_set_text(binding.mirror(), "close");
    check(!binding.target() && !lv_keyboard_get_textarea(keyboard), "Sync overwrote a reentrant detach");
    lv_obj_del(first);
    first = nullptr;
    second = lv_textarea_create(page);
    check(binding.bind(keyboard, second, true), "Could not bind after reentrant detach");
    lv_obj_del(keyboard);
    keyboard = nullptr;
    check(!binding.target(), "Keyboard deletion left live binding");
  }
  // The binding must remove its hooks when it dies before the borrowed fields.
  keyboard = lv_keyboard_create(page);
  {
    ui::widgets::KeyboardBinding binding;
    check(binding.bind(keyboard, second, false), "Could not set up borrowed-field lifetime test");
  }
  check(!lv_keyboard_get_textarea(keyboard), "Destroyed binding left keyboard targeting borrowed field");

  // Native set_text on a length-limited field continues using the object after
  // each VALUE_CHANGED. The binding must stop its own transaction immediately.
  for (int mode = 0; mode < 3; ++mode) {
    ui::widgets::KeyboardBinding binding;
    binding.ensureCreated(24, 52);
    auto *limited = lv_textarea_create(page);
    lv_textarea_set_max_length(limited, 24);
    lv_textarea_set_text(limited, "prefix ř🙂 suffix");
    lv_textarea_set_text(second, "Next field");
    check(binding.bind(keyboard, limited, true), "Restricted mirror bind failed");
    ChangeAction action{&binding, keyboard, second, mode, 0};
    lv_obj_add_event_cb(limited, changeAction, LV_EVENT_VALUE_CHANGED, &action);
    binding.setText(limited, "replacement");
    check(action.calls == 1, "Binding continued editing after DELETE/detach/rebind");
    if (mode == 2)
      check(binding.target() == second && !strcmp(lv_textarea_get_text(binding.mirror()), "Next field"),
            "Old edit overwrote reentrant binding");
    else
      check(!binding.target(), "Canceled edit restored old binding");
    if (mode != 0)
      lv_obj_del(limited);
  }
  {
    ui::widgets::KeyboardBinding binding;
    binding.ensureCreated(24, 52);
    auto *limited = lv_textarea_create(page);
    lv_textarea_set_max_length(limited, 24);
    lv_textarea_set_text(limited, "Ař🙂Z");
    check(binding.bind(keyboard, limited, true), "UTF-8 mirror bind failed");
    int changes = 0;
    lv_obj_add_event_cb(
        limited, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); },
        LV_EVENT_VALUE_CHANGED, &changes);
    lv_textarea_set_cursor_pos(binding.mirror(), 3);
    lv_textarea_add_char(binding.mirror(), 'x');
    check(changes == 1 && !strcmp(lv_textarea_get_text(limited), "Ař🙂xZ") &&
              lv_textarea_get_cursor_pos(limited) == 4,
          "Single keystroke rewrote whole restricted field or lost caret");
    binding.sync();
    check(changes == 1, "Unchanged mirror emitted redundant changes");
    binding.setText(limited, "AŘ🙂xZ");
    check(!strcmp(lv_textarea_get_text(limited), "AŘ🙂xZ") &&
              !strcmp(lv_textarea_get_text(binding.mirror()), "AŘ🙂xZ"),
          "UTF-8 common span split a character");
    ChangeAction action{&binding, keyboard, second, 0, 0};
    lv_obj_add_event_cb(limited, changeAction, LV_EVENT_VALUE_CHANGED, &action);
    lv_textarea_add_char(binding.mirror(), 'q');
    check(!binding.target() && action.calls == 1, "Sync did not survive restricted target deletion");
  }
  {
    ui::widgets::KeyboardBinding binding;
    binding.ensureCreated(24, 52);
    auto *limited = lv_textarea_create(page);
    lv_textarea_set_max_length(limited, 5);
    lv_textarea_set_text(limited, "12XYZ");
    check(binding.bind(keyboard, limited, true), "Clipped value mirror bind failed");
    binding.setText(limited, "abcdeXYZ");
    check(!strcmp(lv_textarea_get_text(limited), "abcde") &&
              !strcmp(lv_textarea_get_text(binding.mirror()), "abcde"),
          "Old suffix consumed new value length budget");
    binding.clear();
    lv_textarea_set_accepted_chars(limited, "123ř");
    lv_textarea_set_text(limited, "12");
    check(binding.bind(keyboard, limited, true), "Accepted-character mirror bind failed");
    check(lv_textarea_get_accepted_chars(binding.mirror()) != nullptr,
          "Mirror lost accepted-character policy");
    binding.setText(limited, "xřa321z");
    check(!strcmp(lv_textarea_get_text(limited), "ř321") &&
              !strcmp(lv_textarea_get_text(binding.mirror()), "ř321"),
          "Programmatic value lost character policy");
    lv_textarea_add_char(binding.mirror(), 'z');
    check(!strcmp(lv_textarea_get_text(binding.mirror()), "ř321"), "Mirror accepted forbidden character");
    binding.clear();
    check(!lv_textarea_get_accepted_chars(binding.mirror()), "Detached mirror retained borrowed policy");
    lv_obj_del(limited);
  }
  lv_obj_del(page);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Keyboard binding leaked mirror tree");
  puts("Keyboard binding: UTF-8 spans, field policies, DELETE, detach and reentrant binding passed.");
}
