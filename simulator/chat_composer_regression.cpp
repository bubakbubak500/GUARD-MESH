// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/ChatComposer.h"
#include "theme/Fonts.h"
#include "widgets/FkeyShape.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Composer = ui::screens::ChatComposer;
std::vector<lv_obj_t *> retired;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Fixture {
  Composer composer;
  lv_obj_t *parent, *messages;
  Composer::Layout layout{240, 280, 0, 0};
  int calls = 0, picks = 0, changes = 0, mode = 0;
  bool success = true, dismissed = false, nested = false;
  std::string sent;
  Composer::Host host() {
    return {this,
            [](void *context, lv_event_t *event) {
              auto &f = *static_cast<Fixture *>(context);
              if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
                ++f.changes;
            },
            [](void *context, Composer::Picker) { ++static_cast<Fixture *>(context)->picks; },
            [](void *context, const char *snapshot, bool dismiss) {
              auto &f = *static_cast<Fixture *>(context);
              ++f.calls;
              f.dismissed = dismiss;
              f.nested = f.composer.send();
              if (f.mode == 1)
                lv_textarea_set_text(f.composer.field(), "New draft");
              if (f.mode == 2)
                f.composer.invalidate();
              if (f.mode == 3)
                lv_obj_del(f.composer.row());
              if (f.mode == 4) {
                f.composer.build(f.parent, f.messages, f.layout, f.host());
                lv_textarea_set_text(f.composer.field(), "New panel");
                check(!f.composer.send(), "Rebuilt composer allowed recursive send");
              }
              // Read AFTER mutating/deleting the original text buffer.
              f.sent = snapshot;
              return f.success;
            },
            [](lv_obj_t **root) {
              lv_obj_add_flag(*root, LV_OBJ_FLAG_HIDDEN);
              retired.push_back(*root);
              *root = nullptr;
            }};
  }
  explicit Fixture(lv_obj_t *container) : parent(container), messages(lv_obj_create(container)) {
    composer.build(parent, messages, layout, host());
  }
};
void click(lv_obj_t *button) { lv_event_send(button, LV_EVENT_CLICKED, nullptr); }
void value(Composer &composer, const char *text) { lv_textarea_set_text(composer.field(), text); }
void position(Composer &composer, int y) {
  lv_obj_update_layout(composer.row());
  check(lv_obj_get_y(composer.row()) == y, "Composer not pinned above keyboard/bottom");
}
} // namespace
void runChatComposerRegression() {
  auto *container = lv_obj_create(lv_layer_top());
  lv_obj_set_size(container, 320, 480);
  lv_obj_set_style_pad_all(container, 0, 0);
  {
    Fixture first(container), second(container);
    const auto base = Composer::baseHeight();
    check(first.composer.height() == base && second.composer.height() == base, "Initial composer height");
    value(first.composer, "one\ntwo\nthree\nfour\nfive");
    check(first.composer.height() == base + 3 * lv_font_get_line_height(&ui::theme::font14()),
          "Composer line cap");
    check(second.composer.height() == base, "Growing one panel changed another");
    first.layout.keyboardHeight = 120;
    first.composer.relayout(first.layout);
    position(first.composer, 280 - 120 - first.composer.height());
    check(lv_obj_get_style_pad_bottom(first.messages, 0) == first.composer.height() + 6,
          "Message inset stale");
    first.layout.width = 320;
    first.composer.relayout(first.layout);
    lv_obj_update_layout(first.composer.row());
    check(lv_obj_get_width(first.composer.row()) == 320, "Composer rotation width stale");
    value(first.composer, "Short");
    check(first.composer.height() == base, "Composer did not shrink");
    position(first.composer, 280 - 120 - base);
    first.layout.keyboardHeight = 0;
    first.composer.relayout(first.layout);
    position(first.composer, 280 - base);
    std::string accented;
    for (int i = 0; i < 120; ++i)
      accented += "ř";
    value(first.composer, accented.c_str());
    check(!lv_obj_has_flag(first.composer.counter(), LV_OBJ_FLAG_HIDDEN) &&
              !strcmp(lv_label_get_text(first.composer.counter()), "120/160"),
          "Counter must count codepoints");
    value(first.composer, "Short");
    check(lv_obj_has_flag(first.composer.counter(), LV_OBJ_FLAG_HIDDEN), "Short draft showed limit counter");
    first.success = false;
    check(!first.composer.send() && !strcmp(lv_textarea_get_text(first.composer.field()), "Short"),
          "Failed send erased draft");
    first.success = true;
    check(first.composer.send(false) && !first.dismissed && !first.nested, "Send policy/recursive guard");
    check(!*lv_textarea_get_text(first.composer.field()) && first.sent == "Short",
          "Successful send did not clear exact draft");
    const int calls = first.calls;
    check(!first.composer.send() && first.calls == calls, "Empty composer sent");
    value(first.composer, "Captured ř🙂");
    first.mode = 1;
    check(first.composer.send() && first.sent == "Captured ř🙂", "Send used mutated textarea buffer");
    check(!strcmp(lv_textarea_get_text(first.composer.field()), "New draft"),
          "Send erased replacement draft");
    first.mode = 2;
    check(first.composer.send() && !strcmp(lv_textarea_get_text(first.composer.field()), "New draft"),
          "Send cleared another conversation");
    first.mode = 0;
    auto *send = first.composer.sendButton();
    lv_event_send(send, LV_EVENT_PRESSED, nullptr);
    first.composer.invalidate();
    const int before = first.calls;
    click(send);
    check(first.calls == before, "Press from former conversation sent new draft");
    click(send);
    check(first.calls == before + 1 && first.dismissed, "Synthetic navigation click failed");
    click(first.composer.emojiButton());
    check(first.picks == 1, "Picker dispatch failed");
    auto *oldSend = first.composer.sendButton();
    auto *oldField = first.composer.field();
    first.composer.build(container, first.messages, first.layout, first.host());
    value(first.composer, "Current");
    const int changes = first.changes;
    click(oldSend);
    lv_textarea_set_text(oldField, "Stale");
    check(first.calls == before + 1 && first.changes == changes, "Retired tree still dispatched");
    first.mode = 4;
    check(first.composer.send() && first.sent == "Current" &&
              !strcmp(lv_textarea_get_text(first.composer.field()), "New panel"),
          "Reentrant rebuild lost new draft");
    first.mode = 3;
    check(first.composer.send() && first.sent == "New panel" && !first.composer.field() &&
              !first.composer.row(),
          "Deleted tree left stale references");
    first.composer.build(container, first.messages, first.layout, first.host());
    auto *orphanSend = first.composer.sendButton();
    lv_obj_del(first.composer.field());
    const int afterDelete = first.calls;
    click(orphanSend);
    check(first.calls == afterDelete && !first.composer.send(), "Deleted field remained sendable");
    lv_obj_del(first.messages);
    first.composer.relayout(first.layout);
    // Shared F-key canvases own their backing allocations through deletion.
    for (int shape = 0; shape < 5; ++shape) {
      auto *button = lv_btn_create(container);
      ui::widgets::styleChipAsFkey(button, nullptr, shape, 0xff0000, 40, true);
      check(lv_obj_get_child_cnt(button) == 1, "F-key canvas missing");
      lv_obj_del(button);
    }
  }
  // Destructors detach listeners even while the closer defers LVGL deletion.
  for (auto *row : retired) {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i)
      lv_event_send(lv_obj_get_child(row, i), LV_EVENT_CLICKED, nullptr);
  }
  retired.clear();
  lv_obj_del(container);
  puts("Chat composer regression passed: per-panel layout, UTF-8 counter, send snapshots, stale events and "
       "lifetime.");
}
