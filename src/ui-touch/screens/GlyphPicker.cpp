// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlyphPicker.h"
#include "../device_caps.h"
#include "../emoji_data.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
namespace ui {
namespace screens {
namespace glyphPicker {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static ObjectRef target, gridRef;
static int selected = -1, columns = 1, accX = 0, accY = 0;
static const char *const *items = nullptr;
static int itemCount = 0;
static Result result = nullptr;
static uint32_t requestId = 0;
static void targetDeleted(lv_event_t *);
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_current_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void clearTarget() {
  if (target.get())
    lv_obj_remove_event_cb(target.get(), targetDeleted);
  target.set(nullptr);
}
static void dismiss(bool restoreFocus) {
  ObjectRef previous;
  previous.set(target.get());
  clearTarget();
  gridRef.set(nullptr);
  selected = -1;
  accX = accY = 0;
  result = nullptr;
  requestId = 0;
  if (root)
    host.closeRoot(&root);
  if (restoreFocus && previous.get() && host.returnFocus)
    host.returnFocus(previous.get());
}
void close() { dismiss(true); }
bool isOpen() { return root != nullptr; }
void configure(const Host &value) {
  dismiss(false);
  host = value;
}
void cancel(Result callback) {
  if (result == callback)
    dismiss(false);
}
void cancelFor(lv_obj_t *object) {
  if (object && target.get() == object)
    dismiss(false);
}
static void targetDeleted(lv_event_t *) {
  // ObjectRef's DELETE callback precedes this callback and clears the pointer.
  dismiss(false);
}
static void gridDeleted(lv_event_t *event) {
  if (owns(event))
    dismiss(false);
}
static void rootDeleted(lv_event_t *event) {
  if (lv_event_get_target(event) != root)
    return;
  root = nullptr;
  clearTarget();
  gridRef.set(nullptr);
  selected = -1;
  accX = accY = 0;
  result = nullptr;
  requestId = 0;
}
static void closeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
      lv_event_get_target(event) == lv_event_get_current_target(event) && owns(event))
    close();
}
static void insertIndex(int index) {
  if (!root)
    return;
  const char *glyph = index >= 0 && index < itemCount ? items[index] : nullptr;
  ObjectRef destination;
  destination.set(target.get());
  const auto callback = result;
  const uint32_t request = requestId;
  // Retire this popup before dispatch: an insertion callback may open another one.
  dismiss(true);
  if (!glyph)
    return;
  if (callback)
    callback(request, glyph);
  else if (destination.get())
    host.insert(destination.get(), glyph);
}
static void pickEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && owns(event))
    insertIndex(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

// Curated insert set, grouped. Kept in sync with what the extras fonts bake
// (see scripts/build/regen-extras-fonts.sh) — a glyph here that isn't in the
// font would just render as the font's notdef box, so only ship baked ones.
static const char *const k_emoji_items[] = {
    // faces
    "\xF0\x9F\x98\x80",
    "\xF0\x9F\x98\x83",
    "\xF0\x9F\x98\x84",
    "\xF0\x9F\x98\x81",
    "\xF0\x9F\x98\x86",
    "\xF0\x9F\x98\x85",
    "\xF0\x9F\x98\x82",
    "\xF0\x9F\x98\x89",
    "\xF0\x9F\x98\x8A",
    "\xF0\x9F\x98\x8D",
    "\xF0\x9F\x98\x98",
    "\xF0\x9F\x98\x8B",
    "\xF0\x9F\x98\x9C",
    "\xF0\x9F\x98\x8E",
    "\xF0\x9F\x98\x8F",
    "\xF0\x9F\x98\x92",
    "\xF0\x9F\x98\x9E",
    "\xF0\x9F\x98\x94",
    "\xF0\x9F\x98\xA2",
    "\xF0\x9F\x98\xAD",
    "\xF0\x9F\x98\xA1",
    "\xF0\x9F\x98\xA0",
    "\xF0\x9F\x98\xB1",
    "\xF0\x9F\x98\xB4",
    "\xF0\x9F\x98\xAC",
    "\xF0\x9F\x98\x90",
    // gestures + people
    "\xF0\x9F\x91\x8D",
    "\xF0\x9F\x91\x8E",
    "\xF0\x9F\x91\x8C",
    "\xE2\x9C\x8C",
    "\xF0\x9F\x91\x8A",
    "\xE2\x9C\x8A",
    "\xF0\x9F\x91\x8F",
    "\xF0\x9F\x99\x8C",
    "\xF0\x9F\x99\x8F",
    "\xF0\x9F\x92\xAA",
    "\xF0\x9F\x91\x8B",
    // hearts
    "\xE2\x9D\xA4",
    "\xF0\x9F\x92\x94",
    "\xF0\x9F\x92\x95",
    "\xF0\x9F\x92\x99",
    "\xF0\x9F\x92\x9A",
    "\xF0\x9F\x92\x9B",
    "\xF0\x9F\x92\x9C",
    // symbols / celebration
    "\xF0\x9F\x8E\x89",
    "\xF0\x9F\x8E\x8A",
    "\xE2\x9C\xA8",
    "\xF0\x9F\x94\xA5",
    "\xF0\x9F\x92\xAF",
    "\xE2\x9C\x85",
    "\xE2\x9D\x8C",
    "\xE2\x9D\x97",
    "\xE2\x9D\x93",
    "\xE2\x9A\xA0",
    "\xF0\x9F\x92\xA9",
    "\xE2\xAD\x90",
    "\xF0\x9F\x8C\x9F",
    "\xE2\x9A\xA1",
    "\xE2\x98\x80",
    "\xE2\x98\x81",
    "\xE2\x9D\x84",
    "\xE2\x98\x94",
    "\xE2\x98\x95",
    "\xF0\x9F\x8D\xBA",
    "\xF0\x9F\x8D\xBB",
    "\xF0\x9F\x8D\x95",
    "\xF0\x9F\x8E\x82",
    "\xF0\x9F\x8E\x81",
    "\xF0\x9F\x92\xAC",
    "\xF0\x9F\x92\xA4",
    "\xF0\x9F\x92\xA5",
    // objects
    "\xF0\x9F\x9A\x80",
    "\xF0\x9F\x93\xB7",
    "\xF0\x9F\x93\xB1",
    "\xF0\x9F\x92\xBB",
    "\xF0\x9F\x93\x8D",
    "\xF0\x9F\x93\x8C",
    "\xF0\x9F\x93\x85",
    "\xF0\x9F\x94\x8B",
    "\xF0\x9F\x93\xA1",
    "\xF0\x9F\x94\x91",
    "\xF0\x9F\x94\x92",
    "\xF0\x9F\x93\xA7",
    "\xF0\x9F\x8F\xA0",
    "\xF0\x9F\x9A\x97",
    "\xE2\x8C\x9A",
    "\xF0\x9F\x92\xA1",
    // animals
    "\xF0\x9F\x90\xB6",
    "\xF0\x9F\x90\xB1",
    "\xF0\x9F\x90\xB8",
    "\xF0\x9F\x90\xBB",
    "\xF0\x9F\x90\xA7",
    "\xF0\x9F\x90\x9D",
    // activity / flags  (soccer, football, moai, transgender flag, Lesotho flag)
    "\xE2\x9A\xBD",
    "\xF0\x9F\x8F\x88",
    "\xF0\x9F\x97\xBF",
    "\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xE2\x9A\xA7\xEF\xB8\x8F",
    "\xF0\x9F\x87\xB1\xF0\x9F\x87\xB8",
    // special characters / punctuation / currency / math
    "\xE2\x80\x94",
    "\xE2\x80\xA6",
    "\xE2\x80\x9C",
    "\xE2\x80\x9D",
    "\xE2\x80\x98",
    "\xE2\x80\x99",
    "\xE2\x86\x92",
    "\xE2\x86\x90",
    "\xC2\xB0",
    "\xC2\xB1",
    "\xC3\x97",
    "\xC3\xB7",
    "\xE2\x82\xAC",
    "\xC2\xA3",
    "\xC2\xA5",
    "\xC2\xA9",
    "\xC2\xAE",
    "\xE2\x84\xA2",
    "\xE2\x89\xA0",
    "\xE2\x89\xA4",
    "\xE2\x89\xA5",
    "\xC2\xBD",
    "\xC2\xBC",
    "\xC2\xBE",
};
static constexpr int k_emoji_count = (int)(sizeof(k_emoji_items) / sizeof(k_emoji_items[0]));

// Special characters / symbols not easy to reach on a compact keyboard — their OWN picker,
// separate from emoji. All render via the font16() chain (montserrat ASCII base + extras_font
// symbols/accents fallback), so no tofu.
static const char *const k_special_items[] = {
    // common ASCII symbols
    "%",
    "$",
    "@",
    "#",
    "&",
    "*",
    "+",
    "=",
    "/",
    "\\",
    "|",
    "<",
    ">",
    "~",
    "^",
    "`",
    "[",
    "]",
    "{",
    "}",
    // currency / math / punctuation
    "\xE2\x82\xAC",
    "\xC2\xA3",
    "\xC2\xA5",
    "\xC2\xB0",
    "\xC2\xB1",
    "\xC3\x97",
    "\xC3\xB7",
    "\xC2\xBD",
    "\xC2\xBC",
    "\xC2\xBE",
    "\xC2\xA7",
    "\xC2\xA9",
    "\xC2\xAE",
    "\xE2\x84\xA2",
    "\xE2\x80\xA2",
    "\xE2\x80\x93",
    "\xE2\x80\x94",
    "\xE2\x80\xA6",
    "\xE2\x86\x92",
    "\xE2\x86\x90",
    "\xE2\x86\x91",
    "\xE2\x86\x93",
    "\xE2\x89\xA0",
    "\xE2\x89\xA4",
    "\xE2\x89\xA5",
    "\xE2\x84\x83",
    "\xE2\x84\x89",
    // accented letters
    "\xC3\xA1",
    "\xC3\xA9",
    "\xC3\xAD",
    "\xC3\xB3",
    "\xC3\xBA",
    "\xC3\xB1",
    "\xC3\xBC",
    "\xC3\xA7",
    "\xC3\x9F",
    "\xC3\xA0",
    "\xC3\xA8",
    "\xC3\xAC",
    "\xC3\xB2",
    "\xC3\xB9",
    "\xC3\xA2",
    "\xC3\xAA",
    "\xC3\xAE",
    "\xC3\xB4",
    "\xC3\xBB",
    "\xC3\xA4",
    "\xC3\xAB",
    "\xC3\xAF",
    "\xC3\xB6",
    "\xC3\xA3",
    "\xC3\xB5",
    "\xC3\xA5",
    "\xC3\xB8",
    "\xC3\xA6",
    // Polish diacritics (Latin Extended-A; o-acute already listed above)
    "\xC4\x85",
    "\xC4\x87",
    "\xC4\x99",
    "\xC5\x82",
    "\xC5\x84",
    "\xC5\x9B",
    "\xC5\xBA",
    "\xC5\xBC",
};
static constexpr int k_special_count = (int)(sizeof(k_special_items) / sizeof(k_special_items[0]));

// Paint the hardware-selected cell highlighted and the rest normal, and keep
// the selection scrolled into view. No-op when nothing is selected (touch-only).
static void paintSelection() {
  if (!gridRef.get())
    return;
  const uint32_t n = lv_obj_get_child_cnt(gridRef.get());
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t *b = lv_obj_get_child(gridRef.get(), i);
    if (!b)
      continue;
    const bool sel = ((int)i == selected);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    setSelectionGlow(b, sel, LV_PART_MAIN);
  }
  if (selected >= 0 && selected < (int)n) {
    lv_obj_t *b = lv_obj_get_child(gridRef.get(), (uint32_t)selected);
    if (b)
      lv_obj_scroll_to_view(b, LV_ANIM_ON);
  }
}

// Trackball is high-resolution: one physical roll emits several motion counts,
// so stepping one cell per raw count made the selector fly across the grid.
// Accumulate raw motion and only advance one cell once the accumulator crosses
// kEmojiSelStep — so it takes a deliberate roll to move each cell.
static constexpr int kEmojiSelStep = MotionStep; // raw counts per one-cell move (higher = less sensitive)

// Feed raw trackball motion; advances the highlighted cell at most one step per
// axis per call. Called from the trackball poll while the emoji sheet is open.
void move(int rawdx, int rawdy) {
  if (!gridRef.get() || itemCount == 0)
    return;
  const int visibleCount = static_cast<int>(lv_obj_get_child_cnt(gridRef.get()));
  if (visibleCount <= 0)
    return;
  if (selected < 0) { // first motion just lands on cell 0
    selected = 0;
    accX = accY = 0;
    paintSelection();
    return;
  }
  const int64_t totalX = int64_t(accX) + rawdx, totalY = int64_t(accY) + rawdy;
  accX = totalX > MotionStep ? MotionStep : totalX < -MotionStep ? -MotionStep : static_cast<int>(totalX);
  accY = totalY > MotionStep ? MotionStep : totalY < -MotionStep ? -MotionStep : static_cast<int>(totalY);
  int dc = 0, dr = 0;
  if (accX >= kEmojiSelStep) {
    dc = 1;
    accX = 0;
  } else if (accX <= -kEmojiSelStep) {
    dc = -1;
    accX = 0;
  }
  if (accY >= kEmojiSelStep) {
    dr = 1;
    accY = 0;
  } else if (accY <= -kEmojiSelStep) {
    dr = -1;
    accY = 0;
  }
  if (dc == 0 && dr == 0)
    return; // not enough travel yet

  const int cols = columns > 0 ? columns : 1;
  int idx = selected;
  if (dc)
    idx += dc; // horizontal: free move across the flat list
  if (dr) {
    const int ni = idx + dr * cols; // vertical: jump a full row
    if (ni >= 0 && ni < visibleCount)
      idx = ni;
  }
  if (idx < 0)
    idx = 0;
  if (idx >= visibleCount)
    idx = visibleCount - 1;
  if (idx != selected) {
    selected = idx;
    paintSelection();
  }
}

// Trackball centre-click while the sheet is open: insert the highlighted glyph.
// Returns true if it consumed the click (so it isn't also injected as a tap).
bool activate() {
  if (!root)
    return false;
  if (selected >= 0 && gridRef.get()) {
    if (auto *cell = lv_obj_get_child(gridRef.get(), selected))
      insertIndex(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(cell))));
  }
  return true; // swallow the click even if nothing selected yet
}

// Opened from the composer's emoji button. `ta` is the composer textarea.
static void build(Set set, const char *pickerTitle) {
  items = set == Set::Emoji ? k_emoji_items : k_special_items;
  itemCount = set == Set::Emoji ? k_emoji_count : k_special_count;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  if (host.privateNavigation)
    lv_obj_add_flag(root, LV_OBJ_FLAG_USER_1);
  lv_obj_set_size(root, sw, sh - host.statusHeight());
  lv_obj_set_pos(root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, closeEvent, LV_EVENT_CLICKED, nullptr);

  const lv_coord_t cardw = sw - 16;
  const lv_coord_t cardh = sh - host.statusHeight() - 16;
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardw, cardh);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR(pickerTitle));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_pos(title, 2, 0);
  addCloseXBadge(card, closeEvent);

  // Hint line: how to use the board's primary picker controls.
  lv_obj_t *hint = lv_label_create(card);
#if defined(HAS_M9_KEYBOARD)
  lv_label_set_text(hint, TR("Arrows \xE2\x80\xA2 OK"));
#else
  lv_label_set_text(hint, TR("Roll to highlight \xE2\x80\xA2 click to insert"));
#endif
  lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -24, 4);

  // Scrollable grid of glyph buttons.
  const lv_coord_t grid_w = cardw - 16;
  lv_obj_t *grid = lv_obj_create(card);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, grid_w, cardh - 16 - 26);
  lv_obj_set_pos(grid, 0, 26);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_column(grid, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  gridRef.set(grid);
  lv_obj_add_event_cb(grid, gridDeleted, LV_EVENT_DELETE, nullptr);
  // Emoji cells render the baked colour glyph as a ZOOMED image (70% bigger than the 16 px
  // font glyph) and get a bigger cell; special-character cells stay text labels.
#if CAP_LARGE_SCREEN
  const bool big_emoji = (items == k_emoji_items);
  const lv_coord_t cell_px = big_emoji ? 48 : 38;
#else
  const lv_coord_t cell_px = 38;
#endif
  // Column count for the trackball selector's row jumps: floor((w + gap) /
  // (cell + gap)), gap=4. Matches the flex-wrap that LVGL computes.
  columns = (int)((grid_w + 4) / (cell_px + 4));
  if (columns < 1)
    columns = 1;
  selected = -1; // start un-highlighted; first roll selects index 0
  accX = accY = 0;

  for (int i = 0; i < itemCount; ++i) {
    lv_obj_t *b = lv_btn_create(grid);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_set_size(b, cell_px, cell_px);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, pickEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
#if CAP_LARGE_SCREEN && LV_USE_IMGFONT
    // Baked colour-emoji → draw it as a zoomed image (1.7× ≈ 70% bigger than the 16 px
    // font glyph). Special chars (no emoji dsc) fall through to the text label below.
    if (big_emoji) {
      uint32_t goff = 0;
      const lv_img_dsc_t *ed = emojiGlyphLookup(_lv_txt_encoded_next(items[i], &goff));
      if (ed) {
        lv_obj_t *im = lv_img_create(b);
        lv_img_set_src(im, ed);
        lv_img_set_antialias(im, true);
        if (ed->header.w && ed->header.h)
          lv_img_set_pivot(im, ed->header.w / 2, ed->header.h / 2);
        lv_img_set_zoom(im, 435); // 256 = 1×, 435 ≈ 1.7×
        lv_obj_center(im);
        continue;
      }
    }
#endif
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, items[i]);
    lv_obj_set_style_text_font(l, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
  }
  if (host.selectFirst && itemCount > 0) {
    selected = 0;
    paintSelection();
  }
}

void open(lv_obj_t *object, Set set) {
  dismiss(false);
  if (!object || !target.set(object))
    return;
  lv_obj_add_event_cb(object, targetDeleted, LV_EVENT_DELETE, nullptr);
  build(set, set == Set::Emoji ? "Insert emoji / symbol" : "Special characters");
}
void pick(Result callback, uint32_t request, const char *title) {
  dismiss(false);
  if (!callback)
    return;
  build(Set::Emoji, title);
  result = callback;
  requestId = request;
}
} // namespace glyphPicker
} // namespace screens
} // namespace ui
