// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccentPicker.h"
#include "../models/AccentCharacters.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
#include <cstring>
namespace ui {
namespace screens {
namespace accentPicker {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef target;
static const accents::AccentSet *variants = nullptr;
static char *original = nullptr;
static uint32_t originalCursor = 0;
static size_t caretByte = 0;
static bool navigating = false;
static int selected = 0;
static constexpr auto SkipNavigation = LV_OBJ_FLAG_USER_1;
static void targetDeleted(lv_event_t *);
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void clearState() {
  if (target.get())
    lv_obj_remove_event_cb(target.get(), targetDeleted);
  target.set(nullptr);
  platform::release(original);
  original = nullptr;
  variants = nullptr;
  navigating = false;
  selected = 0;
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
void configure(const Host &value) {
  close();
  host = value;
}
void cancelFor(lv_obj_t *field) {
  if (field && field == target.get())
    close();
}
bool isOpen() { return root != nullptr; }
bool navigationActive() { return isOpen() && navigating; }
static void restyle() {
  if (!root)
    return;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    setSelectionGlow(lv_obj_get_child(root, i), navigating && int(i) == selected, LV_PART_MAIN);
}
bool enterNavigation() {
  if (!root || !lv_obj_get_child_cnt(root))
    return false;
  navigating = true;
  selected = 0;
  restyle();
  return true;
}
void move(int delta) {
  if (!navigationActive())
    return;
  const int size = int(lv_obj_get_child_cnt(root));
  if (!size) {
    close();
    return;
  }
  selected = (selected % size + delta % size + size) % size;
  restyle();
}
void confirm() {
  if (!navigationActive())
    return;
  if (selected >= 0 && selected < int(lv_obj_get_child_cnt(root)))
    lv_event_send(lv_obj_get_child(root, selected), LV_EVENT_CLICKED, nullptr);
  else
    close();
}
static void cellClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !owns(event))
    return;
  const int index = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  auto *field = target.get();
  if (!field || !variants || index < 0 || index >= variants->n || !original ||
      lv_textarea_get_cursor_pos(field) != originalCursor || strcmp(lv_textarea_get_text(field), original)) {
    close();
    return;
  }
  const char *variant = variants->v[index];
  const size_t length = strlen(original), size = strlen(variant);
  char *text = static_cast<char *>(platform::allocate(length + size, true));
  if (!text) {
    close();
    return;
  }
  memcpy(text, original, caretByte - 1);
  memcpy(text + caretByte - 1, variant, size);
  memcpy(text + caretByte - 1 + size, original + caretByte, length - caretByte + 1);
  const uint32_t cursor = originalCursor; // one codepoint replaces one ASCII codepoint
  ObjectRef destination;
  destination.set(field);
  const auto apply = host.apply;
  close();
  if (destination.get() && apply)
    apply(destination.get(), text, cursor);
  platform::release(text);
}
bool show(lv_obj_t *field) {
  close();
  if (!field || !host.apply)
    return false;
  const char *text = lv_textarea_get_text(field);
  const uint32_t cursor = lv_textarea_get_cursor_pos(field);
  uint32_t byte = 0, cp = 0;
  while (text[byte] && cp < cursor) {
    _lv_txt_encoded_next(text, &byte);
    ++cp;
  }
  if (!byte || cp != cursor)
    return false;
  const char key[] = {text[byte - 1], 0};
  const auto *set = accents::lookup(key);
  if (!set)
    return false;
  const size_t length = strlen(text);
  if (length > SIZE_MAX - 8)
    return false;
  original = static_cast<char *>(platform::allocate(length + 1, true));
  if (!original)
    return false;
  memcpy(original, text, length + 1);
  if (!target.set(field)) {
    close();
    return false;
  }
  lv_obj_add_event_cb(field, targetDeleted, LV_EVENT_DELETE, nullptr);
  variants = set;
  originalCursor = cursor;
  caretByte = byte;
  const int cw = 34, ch = 40, gap = 4, pad = 6;
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_add_flag(root,
                  SkipNavigation); // passive tap-only hint: never a keyboard-nav focus target (issue #22)
  lv_obj_remove_style_all(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(colors().COLOR_ACCENT_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, pad, LV_PART_MAIN);
  lv_obj_set_style_pad_column(root, gap, LV_PART_MAIN);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(root, set->n * cw + (set->n - 1) * gap + pad * 2, ch + pad * 2);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  for (uint8_t i = 0; i < set->n; ++i) {
    lv_obj_t *c = lv_btn_create(root);
    lv_obj_add_flag(c, SkipNavigation); // tappable, but never a keyboard-nav focus stop
    lv_obj_set_size(c, cw, ch);
    lv_obj_set_style_radius(c, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, lv_color_hex(colors().COLOR_ACCENT_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(c, cellClicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(intptr_t(i)));
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, set->v[i]);
    lv_obj_set_style_text_font(l, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
  }

  lv_obj_update_layout(root);
  lv_area_t area;
  lv_obj_get_coords(field, &area);
  const lv_coord_t width = lv_obj_get_width(root), height = lv_obj_get_height(root);
  lv_coord_t top = 0, bottom = lv_disp_get_ver_res(nullptr);
  if (host.bounds)
    host.bounds(top, bottom);
  lv_coord_t y = area.y1 - height - 4;
  if (y < top)
    y = area.y2 + 4;
  if (y + height > bottom)
    y = bottom - height;
  if (y < top)
    y = top;
  lv_obj_set_pos(root, (lv_disp_get_hor_res(nullptr) - width) / 2, y);
  lv_obj_move_foreground(root);
  return true;
}
} // namespace accentPicker
} // namespace screens
} // namespace ui
