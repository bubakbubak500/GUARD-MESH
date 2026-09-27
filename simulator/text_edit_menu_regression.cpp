// SPDX-License-Identifier: GPL-3.0-or-later
#include "PasteHexKey.h"
#include "screens/TextEditMenu.h"
#include "widgets/TextSelection.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
namespace menu = ui::screens::textEditMenu;
namespace selection = ui::widgets::textSelection;
char clipboard[700];
int copies, symbols, changes;
bool cut, replaceOnCopy, deleteOnClose, extractKey;
lv_obj_t *originalField = nullptr;
lv_obj_t *mirror = nullptr;
lv_obj_t *replacement = nullptr;
lv_obj_t *symbolTarget = nullptr;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
lv_obj_t *action(int i) { return lv_obj_get_child(root(), i); }
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
void text(lv_obj_t *field, const char *value, int cursor = -1) {
  lv_textarea_set_text(field, value);
  lv_textarea_set_cursor_pos(field, cursor < 0 ? LV_TEXTAREA_CURSOR_LAST : cursor);
  selection::clear(field);
}
menu::Host host() {
  return {[](lv_obj_t **popup) {
            lv_obj_add_flag(*popup, LV_OBJ_FLAG_HIDDEN);
            *popup = nullptr;
            if (deleteOnClose) {
              deleteOnClose = false;
              lv_obj_del(replacement);
            }
          },
          [](lv_obj_t *field) { return field == originalField && mirror ? mirror : field; },
          [] { return static_cast<const char *>(clipboard); },
          [](const char *value, bool wasCut) {
            ++copies;
            cut = wasCut;
            snprintf(clipboard, sizeof clipboard, "%s", value);
            if (replaceOnCopy)
              menu::show(replacement);
          },
          [](lv_obj_t *, const char *value, char *scratch, size_t capacity) {
            return PasteHexKey::extract(value, extractKey ? 32 : 0, scratch, capacity);
          },
          [](lv_obj_t *field) {
            ++symbols;
            symbolTarget = field;
          },
          [](lv_coord_t &top, lv_coord_t &bottom) {
            top = 24;
            bottom = 220;
          }};
}
} // namespace
void runTextEditMenuRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fields = lv_obj_create(lv_layer_top());
  auto *first = lv_textarea_create(fields);
  auto *second = lv_textarea_create(fields);
  auto *mirrorField = lv_textarea_create(fields);
  copies = symbols = changes = 0;
  clipboard[0] = 0;
  cut = replaceOnCopy = deleteOnClose = extractKey = false;
  originalField = mirror = nullptr;
  replacement = second;
  symbolTarget = nullptr;
  menu::configure(host());
  selection::reset();
  text(first, "Ař🙂 Z", 3);
  selection::select(first, 1, 3);
  menu::show(first);
  click(action(1));
  check(copies == 1 && !cut && !strcmp(clipboard, "ř🙂") && !menu::isOpen(), "Edit menu UTF-8 copy failed");
  selection::select(first, 1, 3);
  menu::show(first);
  click(action(0));
  check(cut && !strcmp(clipboard, "ř🙂") && !strcmp(lv_textarea_get_text(first), "A Z") &&
            lv_textarea_get_cursor_pos(first) == 1,
        "Edit menu cut range failed");
  selection::select(first, 0, 1);
  menu::show(first);
  lv_obj_add_event_cb(first, [](lv_event_t *) { ++changes; }, LV_EVENT_VALUE_CHANGED, nullptr);
  click(action(2));
  check(changes == 1 && !strcmp(lv_textarea_get_text(first), "ř🙂 Z") &&
            lv_textarea_get_cursor_pos(first) == 2,
        "Edit menu paste was not one UTF-8 replacement");
  menu::show(first);
  click(action(3));
  uint32_t a, b;
  check(selection::range(first, &a, &b) && a == 0 && b == 4, "Edit menu select-all failed");
  menu::show(first);
  click(action(4));
  check(symbols == 1 && symbolTarget == first && !menu::isOpen(), "Edit menu symbols targeted wrong field");
  text(first, "first");
  menu::show(first);
  auto *oldRoot = root();
  auto *oldCopy = action(1);
  text(second, "second");
  menu::show(second);
  const int before = copies;
  click(oldCopy);
  lv_obj_del(oldRoot);
  check(menu::isOpen() && copies == before, "Old edit menu affected replacement");
  click(action(1));
  check(!strcmp(clipboard, "second"), "Replacement edit menu copied old target");
  text(first, "before");
  menu::show(first);
  text(first, "changed");
  click(action(0));
  check(!strcmp(lv_textarea_get_text(first), "changed") && copies == before + 1,
        "Stale edit menu overwrote changed text");
  menu::show(first);
  lv_textarea_set_cursor_pos(first, 0);
  click(action(0));
  check(!strcmp(lv_textarea_get_text(first), "changed"), "Stale edit menu followed cursor");
  menu::show(first);
  selection::select(first, 0, 2);
  click(action(0));
  check(!strcmp(lv_textarea_get_text(first), "changed"), "Edit menu ignored changed selection");
  // Original/mirror identity and selection are captured at opening, not click time.
  text(first, "mirror text", 6);
  text(mirrorField, "mirror text", 6);
  originalField = first;
  mirror = mirrorField;
  selection::select(first, 0, 6);
  menu::show(first);
  click(action(1));
  check(!strcmp(clipboard, "mirror"), "Edit menu did not transfer original selection to mirror");
  menu::show(first);
  mirror = second;
  click(action(0));
  check(!strcmp(lv_textarea_get_text(mirrorField), "mirror text"), "Rebound mirror accepted old menu action");
  mirror = mirrorField;
  menu::show(first);
  lv_obj_del(first);
  check(!menu::isOpen(), "Original field DELETE left mirror edit menu");
  first = lv_textarea_create(fields);
  originalField = mirror = nullptr;
  text(first, "r");
  menu::show(first);
  lv_obj_del(root());
  check(!menu::isOpen(), "Edit menu root DELETE left state");
  menu::show(first);
  menu::cancelFor(nullptr);
  menu::cancelFor(second);
  check(menu::isOpen(), "Edit menu cancelled for unrelated field");
  menu::cancelFor(first);
  check(!menu::isOpen(), "Edit menu cancelFor failed");
  text(first, "");
  strcpy(clipboard, "key: 0123456789abcdef0123456789abcdef end");
  extractKey = true;
  lv_textarea_set_max_length(first, 32);
  menu::show(first);
  click(action(2));
  check(!strcmp(lv_textarea_get_text(first), "0123456789abcdef0123456789abcdef"),
        "Edit menu lost key extraction before length cap");
  extractKey = false;
  text(first, "keep", 2);
  clipboard[0] = 0;
  menu::show(first);
  click(action(2));
  check(!strcmp(lv_textarea_get_text(first), "keep"), "Empty clipboard changed field");
  text(first, "one");
  text(second, "two");
  replaceOnCopy = true;
  menu::show(first);
  click(action(1));
  check(menu::isOpen(), "Copy completion closed reentrant edit menu");
  replaceOnCopy = false;
  click(action(1));
  check(!strcmp(clipboard, "two"), "Reentrant edit menu lost target");
  text(first, "gone");
  replacement = first;
  menu::show(first);
  deleteOnClose = true;
  click(action(0));
  check(!menu::isOpen(), "Edit menu used target deleted during close");
  menu::configure({});
  selection::reset();
  while (lv_obj_get_child_cnt(lv_layer_top()) > roots)
    lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Edit menu leaked roots");
  std::puts("Text edit menu: cut/copy/paste, key extraction, snapshots, mirror binding, stale trees, DELETE "
            "and reentrancy passed.");
}
