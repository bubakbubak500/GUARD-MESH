// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextEditMenu.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/TextSelection.h"
#include <cstring>
#include <memory>
namespace ui {
namespace screens {
namespace textEditMenu {
using namespace theme;
using widgets::ObjectRef;
namespace selection = widgets::textSelection;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef origin, target;
static char *original = nullptr;
static uint32_t cursor = 0, first = 0, last = 0;
static bool selected = false;
enum Action { Cut, Copy, Paste, All, Symbols };
static void fieldDeleted(lv_event_t *);
static void clearState() {
  if (target.get())
    lv_obj_remove_event_cb(target.get(), fieldDeleted);
  if (origin.get() && origin.get() != target.get())
    lv_obj_remove_event_cb(origin.get(), fieldDeleted);
  target.set(nullptr);
  origin.set(nullptr);
  platform::release(original);
  original = nullptr;
  selected = false;
}
void close() {
  auto *old = root;
  root = nullptr;
  clearState();
  if (old) {
    if (host.closeRoot)
      host.closeRoot(&old);
    else
      lv_obj_del(old);
  }
}
static void fieldDeleted(lv_event_t *) { close(); }
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
  if (field && (field == target.get() || field == origin.get()))
    close();
}
bool isOpen() { return root != nullptr; }
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void actionClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !owns(event))
    return;
  const auto action = static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (action < Cut || action > Symbols)
    return;
  auto *field = target.get();
  uint32_t a = 0, b = 0;
  const bool liveSelection = field && selection::range(field, &a, &b);
  if (!field || !origin.get() || (host.resolve && host.resolve(origin.get()) != field) ||
      strcmp(lv_textarea_get_text(field), original) || lv_textarea_get_cursor_pos(field) != cursor ||
      liveSelection != selected || (selected && (a != first || b != last))) {
    close();
    return;
  }
  ObjectRef destination;
  destination.set(field);
  const auto callbacks = host;
  const uint32_t position = cursor, start = first, end = last;
  const bool hasSelection = selected;
  std::unique_ptr<char, decltype(&platform::release)> text(original, platform::release);
  original = nullptr;
  close();
  if (!destination.get() || strcmp(lv_textarea_get_text(destination.get()), text.get()) ||
      lv_textarea_get_cursor_pos(destination.get()) != position)
    return;
  field = destination.get();
  if (action == Symbols) {
    if (callbacks.symbols)
      callbacks.symbols(field);
    return;
  }
  if (action == All) {
    selection::select(field, 0, _lv_txt_get_encoded_length(text.get()));
    return;
  }
  if (action == Copy || action == Cut) {
    const size_t begin = hasSelection ? selection::cpToByte(text.get(), start) : 0;
    const size_t finish = hasSelection ? selection::cpToByte(text.get(), end) : strlen(text.get());
    const char saved = text.get()[finish];
    text.get()[finish] = 0;
    if (callbacks.copy)
      callbacks.copy(text.get() + begin, action == Cut);
    text.get()[finish] = saved;
    if (!destination.get() || strcmp(lv_textarea_get_text(destination.get()), text.get()) ||
        lv_textarea_get_cursor_pos(destination.get()) != position)
      return;
    if (action == Cut)
      selection::erase(destination.get(), hasSelection ? start : 0,
                       hasSelection ? end : _lv_txt_get_encoded_length(text.get()));
    else
      selection::clear(destination.get());
  } else if (action == Paste && callbacks.clipboard) {
    const char *clipboard = callbacks.clipboard();
    if (!clipboard || !*clipboard)
      return;
    char scratch[65]; // host may extract a complete 16/32-byte hex key
    const char *value =
        callbacks.paste ? callbacks.paste(field, clipboard, scratch, sizeof scratch) : clipboard;
    if (destination.get() && value)
      selection::replace(destination.get(), hasSelection ? start : position, hasSelection ? end : position,
                         value);
  }
}
bool show(lv_obj_t *field) {
  ObjectRef requested;
  if (!field || !requested.set(field))
    return false;
  close();
  if (!requested.get() || root)
    return false;
  auto *resolved = host.resolve ? host.resolve(requested.get()) : requested.get();
  if (!resolved || !origin.set(requested.get()) || !target.set(resolved)) {
    close();
    return false;
  }
  lv_obj_add_event_cb(resolved, fieldDeleted, LV_EVENT_DELETE, nullptr);
  if (origin.get() != resolved)
    lv_obj_add_event_cb(origin.get(), fieldDeleted, LV_EVENT_DELETE, nullptr);
  // A selection made on the original field must follow the visible mirror.
  uint32_t a, b;
  if (resolved != origin.get() && selection::range(origin.get(), &a, &b) &&
      !strcmp(lv_textarea_get_text(resolved), lv_textarea_get_text(origin.get())))
    selection::select(resolved, a, b);
  const char *text = lv_textarea_get_text(resolved);
  original = static_cast<char *>(platform::allocate(strlen(text) + 1, true));
  if (!original) {
    close();
    return false;
  }
  memcpy(original, text, strlen(text) + 1);
  cursor = lv_textarea_get_cursor_pos(resolved);
  selected = selection::range(resolved, &first, &last);
  static const char *const kLabels[] = {"Cut", "Copy", "Paste", "All", "Sym"};
  const int n = 5, cw = 48, ch = 34, gap = 4, pad = 6;
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(colors().COLOR_ACCENT_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, pad, LV_PART_MAIN);
  lv_obj_set_style_pad_column(root, gap, LV_PART_MAIN);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(root, n * cw + (n - 1) * gap + pad * 2, ch + pad * 2);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < n; ++i) {
    lv_obj_t *c = lv_btn_create(root);
    lv_obj_set_size(c, cw, ch);
    lv_obj_set_style_radius(c, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, lv_color_hex(colors().COLOR_ACCENT_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(c, actionClicked, LV_EVENT_CLICKED, reinterpret_cast<void *>((intptr_t)i));
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, kLabels[i]);
    lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
  }

  lv_obj_update_layout(root);
  lv_area_t area;
  lv_obj_get_coords(origin.get(), &area);
  const lv_coord_t width = lv_obj_get_width(root), height = lv_obj_get_height(root);
  lv_coord_t top = 0, bottom = lv_disp_get_ver_res(nullptr);
  if (host.bounds)
    host.bounds(top, bottom);
  lv_coord_t x = (lv_disp_get_hor_res(nullptr) - width) / 2;
  if (x < 2)
    x = 2;
  lv_coord_t y = area.y1 - height - 4;
  if (y < top)
    y = area.y2 + 4;
  if (y + height > bottom)
    y = bottom - height;
  if (y < top)
    y = top;
  lv_obj_set_pos(root, x, y);
  lv_obj_move_foreground(root);
  return true;
}
} // namespace textEditMenu
} // namespace screens
} // namespace ui
