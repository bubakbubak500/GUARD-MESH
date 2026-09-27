#include "Styles.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../device_caps.h"
#include <cstring>
namespace ui { namespace widgets {
using namespace ui::theme;
void styleSurface(lv_obj_t* obj, uint32_t bg, lv_coord_t radius) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
}

void styleCard(lv_obj_t* obj) {
  styleSurface(obj, colors().COLOR_PANEL, 10);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
}

uint32_t lightSurfaceTextRgb(uint32_t color) {
#if defined(HAS_TDECK_PRO)
  (void)color;
  return 0x000000;
#else
  return color;
#endif
}

lv_color_t lightSurfaceTextColor(uint32_t color) {
  return lv_color_hex(lightSurfaceTextRgb(color));
}

void normalizeLightSurfaceRecolor(char* text) {
#if defined(HAS_TDECK_PRO)
  if (!text) return;
  for (char* p = text; *p; ++p) {
    if (*p != '#') continue;
    bool is_color = true;
    for (int i = 1; is_color && i <= 6; ++i) {
      const char c = p[i];
      is_color = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F');
    }
    if (is_color && p[7] == ' ') memset(p + 1, '0', 6);
  }
#else
  (void)text;
#endif
}

#if defined(HAS_TDECK_PRO)
void styleEpaperControlOutline(lv_obj_t* obj, lv_style_selector_t selector) {
  lv_obj_set_style_border_color(obj, lv_color_black(), selector);
  lv_obj_set_style_border_width(obj, 2, selector);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, selector);
}
#endif

// LVGL draws a text area's PLACEHOLDER from LV_PART_TEXTAREA_PLACEHOLDER, and that
// part does NOT inherit the LV_PART_MAIN font the creation sites set. So the hint
// text fell back to the theme's plain Montserrat and every accented character in it
// rendered as a box -- "H(box)l(box)zat n(box)v" for "Halozat nev" -- while text the
// user typed into the same field was fine. 27 placeholders, and the part was styled
// in none of them. The beta_62 sweep fixed ~79 LABELS; this is the other LVGL part
// it never touched.
//
// Resolve the chained face from whatever MAIN already carries, so this cannot change
// how any field looks -- only which glyphs it can draw. Placeholders are often set
// BEFORE the site assigns MAIN's font, so an unrecognised font falls back to
// font14() (the size every such site ends up using anyway).
void taSetPlaceholder(lv_obj_t* ta, const char* txt) {
  if (!ta) return;
  lv_textarea_set_placeholder_text(ta, txt);
  const lv_font_t* f = lv_obj_get_style_text_font(ta, LV_PART_MAIN);
  if      (f == &lv_font_montserrat_12) f = &font12();
  else if (f == &lv_font_montserrat_14) f = &font14();
  else if (f == &lv_font_montserrat_16) f = &font16();
  else if (f != &font12() && f != &font14() && f != &font16()) f = &font14();
  lv_obj_set_style_text_font(ta, f, LV_PART_TEXTAREA_PLACEHOLDER);
}

void styleButton(lv_obj_t* obj) {
  // Subdued slate chip. Background sits at the panel colour with a low
  // overall opacity so the pure-black BG bleeds through — chips read as
  // "etched out of the bezel" rather than glowing. Border is a faint
  // 1-px line of colors().COLOR_ACCENT at low opacity, text is clean white-ish.
  // Press state flashes a brighter slate fill so taps still register.
  // Primary action buttons (Send / Save / Login / Apply / Add) override
  // the bg to colors().COLOR_STATUS_OK so they remain visually distinct.
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  styleEpaperControlOutline(obj, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
#else
  lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(colors().COLOR_ACCENT_PRESS), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(obj, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
#endif
  // ...and the font, for the same reason as the text colour. A button carries a
  // font from the LVGL theme which its child label inherits, and that one has no
  // fallback chain — so accents inside button labels came out as tofu boxes
  // ("Eszk□z □jraind□t□sa") even after the screen-level default was fixed, because
  // the button sits between the screen and the label. font14() is the same face
  // and metrics plus the accent/Greek/Cyrillic/Arabic fallbacks. Labels that set
  // their own font are unaffected — a style on the label beats one on the button.
  lv_obj_set_style_text_font(obj, &font14(), LV_PART_MAIN);
  lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}

// "X" close affordance for popup cards. A bare 16-px glyph with an
// Compact 24×24 visual target with a 32×32 extended hit area, so it reads as
// just a symbol but remains forgiving to hit. Sits inside the card's top-right
// corner — no border / bg, so it doesn't compete visually with the card's
// own buttons.
// IMPORTANT: each caller is responsible for keeping the top-right ~32×32
// of the card content-free (or for placing only short titles there) so
// the X doesn't sit on top of a real button.
// Tanmatsu: tint a close/cancel ✕ glyph red (matches the F1 / Power button). No-op on other boards.
void tanCloseRed(lv_obj_t* lbl) {
#if defined(HAS_TANMATSU)
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xE05544), LV_PART_MAIN);
#else
  (void)lbl;
#endif
}
lv_obj_t* addCloseXBadge(lv_obj_t* card, lv_event_cb_t cb, void* user_data) {
  lv_obj_t* x = lv_obj_create(card);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 24, 24);
  // Keep the visible focus tint tight around the centred glyph. ext_click_area
  // restores the previous forgiving 32-px touch target without painting it.
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -2, 2);
  lv_obj_set_ext_click_area(x, 4);
  // No fill / border — bare glyph. Pressed state nudges a faint dim so
  // there's *some* visual feedback on tap.
  lv_obj_set_style_bg_opa(x, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(x, lv_color_hex(themeRole(0xFFFFFF, colors().COLOR_CONTROL_PRESSED)), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(x, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(x, 12, LV_PART_MAIN);
  lv_obj_set_style_border_width(x, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(x, 0, LV_PART_MAIN);
  lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(x, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
  // The shared navigation cursor adds its accent glow outside this 24-px target;
  // the larger 32-px touch target stays invisible.
  lv_obj_clear_flag(x, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(x);
  lv_obj_add_event_cb(x, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_t* lbl = lv_label_create(x);
  lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(lbl, uiChromeFont(), LV_PART_MAIN);
#if defined(HAS_TANMATSU)
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xE05544), LV_PART_MAIN);   // red ✕ — matches the F1 / Power button
#else
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
#endif
  lv_obj_center(lbl);
  return x;
}

static lv_style_t s_selection_glow_style;
static lv_style_t s_selection_glow_contrast_style;
static bool s_selection_glow_style_ready = false;
static void initSelectionGlowStyle(lv_style_t* style, uint32_t color) {
  lv_style_init(style);
  lv_style_set_outline_width(style, 3);
  // Keep the full cursor inside the widget; outward outlines and shadows are
  // clipped unevenly when a control sits against a scroll-container edge.
  lv_style_set_outline_pad(style, -2);
  lv_style_set_outline_opa(style, LV_OPA_COVER);
  lv_style_set_outline_color(style, lv_color_hex(color));
}
void setSelectionGlow(lv_obj_t* obj, bool selected, lv_style_selector_t selector) {
  if (!obj) return;
  if (!s_selection_glow_style_ready) {
    initSelectionGlowStyle(&s_selection_glow_style, colors().COLOR_ACCENT);
    initSelectionGlowStyle(&s_selection_glow_contrast_style,
                           isDay() ? 0x000000 : 0xFFFFFF);
    s_selection_glow_style_ready = true;
  }
  const bool knob_cursor = lv_obj_check_type(obj, &lv_switch_class) ||
                           lv_obj_check_type(obj, &lv_slider_class);
  const lv_style_selector_t cursor_selector = knob_cursor
      ? (lv_style_selector_t)(LV_PART_KNOB | (selector & LV_STATE_ANY))
      : selector;
  const uint32_t cursor_part = knob_cursor ? LV_PART_KNOB : LV_PART_MAIN;
  lv_obj_remove_style(obj, &s_selection_glow_style, cursor_selector);
  lv_obj_remove_style(obj, &s_selection_glow_contrast_style, cursor_selector);
  if (selected) {
    const bool on_accent = lv_obj_get_style_bg_opa(obj, cursor_part) == LV_OPA_COVER &&
        lv_color_to32(lv_obj_get_style_bg_color(obj, cursor_part)) ==
        lv_color_to32(lv_color_hex(colors().COLOR_ACCENT));
    lv_obj_add_style(obj, on_accent ? &s_selection_glow_contrast_style
                                    : &s_selection_glow_style, cursor_selector);
  }
}

void setNavSelectionGlow(lv_obj_t* obj, bool selected) {
  setSelectionGlow(obj, selected, LV_PART_MAIN | LV_STATE_FOCUSED);
  setSelectionGlow(obj, selected, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

void refreshSelectionColors() {
  if (!s_selection_glow_style_ready) return;
  lv_style_set_outline_color(&s_selection_glow_style, lv_color_hex(colors().COLOR_ACCENT));
  lv_style_set_outline_color(&s_selection_glow_contrast_style, lv_color_hex(isDay() ? 0x000000 : 0xFFFFFF));
  lv_obj_report_style_change(&s_selection_glow_style);
  lv_obj_report_style_change(&s_selection_glow_contrast_style);
}
} }
