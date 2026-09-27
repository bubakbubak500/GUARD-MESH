#include "Fonts.h"
#include "../device_caps.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../emoji_data.h"
namespace ui { namespace theme {
// LVGL 8.3 / Montserrat doesn't ship a STAR glyph. We carry a small custom
// font subset (one glyph: U+2605 BLACK STAR at size 28) in
// star_font_28.c — see &star_font_28 below. Pair the BIG macro with that
// font; the small ASCII macro stays as a plain '*' for places that still
// render in Montserrat (e.g. the action-sheet menu rows at font 12).
// FontAwesome glyphs (monochrome, bpp4) carried by person_font: U+F007 "user",
// U+F519 "tower-broadcast", U+F0C0 "users" (group). Spliced into the g_font_16
// and g_font_14 fallback chains in initTouchFontFallbacks, so these PUA codepoints
// render an icon that follows the label's text colour. Used for the Contacts tab
// icon (person), the Contacts-list type icons (person = peer, antenna = repeater),
// and the Chats list (person = DM, group = channel).
extern "C" const lv_font_t person_font;     // 16 px — for g_font_16 contexts
extern "C" const lv_font_t person_font14;   // 14 px — for g_font_14 (chat list rows)
// FontAwesome "magnifying-glass" (U+F002), 16 px — for the map zoom button. Its
// own one-glyph font, spliced into the g_font_16 fallback chain like person_font.
// FontAwesome sun (U+F185) + moon (U+F186) for sleep-state markers. Spliced into
// the g_font_12, g_font_14, and g_font_16 chains in initTouchFontFallbacks so the
// glyphs render at any of those sizes (the status-bar sleep icon uses g_font_12;
// the battery-chart sleep markers will use g_font_14).
// FontAwesome lock (U+F023) + bell (U+F0F3) + bell-slash (U+F1F6), 16 px — for the
// control-center chips (real lock instead of an eye; a dynamic bell that gains a slash
// when notifications are silenced). Own font (scripts-generated from fa-solid-900),
// spliced into the g_font_16 fallback chain like person_font/zoom_font.

// Extras fallback fonts — em-dash (U+2014), ellipsis (U+2026), middle dot
// (U+00B7). LVGL's stock Montserrat subset doesn't include these, so any
// string carrying them renders missing-glyph rectangles (visible in the
// settings page, toasts, etc.). We carry tiny custom subsets at the three
// label sizes we use (12, 14, 16) and chain them as fallback fonts via
// initTouchFontFallbacks() — runs once at the top of UITask::begin after
// lv_init. Anywhere the touch UI references a Montserrat font, swap to
// &g_font_NN to pick up the fallback chain.
// extras_lat_28 used to be compiled HAS_TANMATSU-only (Large/Huge UI-scale Latin-
// accent fallback, issue #129); it's now compiled on every board (extras_lat_28.c)
// for the "at a glance" notification's message body (atGlanceEnsureFont()) --
// montserrat_28 was picked over 24 because only 12/14/16/28 are actually built
// into this project's vendored LVGL config (LV_FONT_MONTSERRAT_24 isn't enabled).
// extras_lat_20 was HAS_TANMATSU-only too (Large/Huge UI-scale fallback); the
// T-Deck now also builds it, for its 20px at-glance body experiment (see
// atGlanceEnsureFont()) -- extras_lat_20.c's own gate was widened to match.
#if defined(HAS_TANMATSU) || defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
#endif
#if defined(TLORA_PAGER)
// Full multilingual fallbacks at the Pager's accessible text sizes. These keep
// accented Latin, Greek, Cyrillic, Arabic, punctuation, and units aligned with
// the enlarged Montserrat primary instead of dropping back to 12/14/16 px.
#endif
#if defined(HAS_TANMATSU)
// Compressed Latin-accent fonts at the scaled sizes so umlauts etc. match their
// neighbours at Large/Huge UI scale (issue #129). Room for these was made by
// storing the extras fonts compressed (LV_USE_FONT_COMPRESSED) — nothing lost,
// net binary SMALLER, so the P4 app-load ceiling is respected.
#endif
static lv_font_t g_font_12;
static lv_font_t g_font_14;
static lv_font_t g_font_16;

// Swap a label's RESOLVED raw Montserrat for its chained twin (g_font_NN), so
// accents, Greek, Cyrillic and Arabic reach the fallback fonts instead of
// rendering as tofu boxes. LVGL's theme puts a font on each widget, so a label
// that sets none of its own lands on the stock LV_FONT_MONTSERRAT_* — which has
// no fallback chain. That is the Hungarian "Lek□rdez□s (perc)" class of bug.
//
// Reads what the label actually resolves to rather than assuming 14, so a label
// inheriting a larger font from its parent keeps that size. Anything already
// chained, or using a deliberately different font, is left alone — so this is
// safe to call after any lv_label_create.
void useChainedFont(lv_obj_t* label) {
  if (!label) return;
  const lv_font_t* f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const lv_font_t* repl = nullptr;
  if      (f == &lv_font_montserrat_12) repl = &g_font_12;
  else if (f == &lv_font_montserrat_14) repl = &g_font_14;
  else if (f == &lv_font_montserrat_16) repl = &g_font_16;
  if (repl) lv_obj_set_style_text_font(label, repl, LV_PART_MAIN);
}

// Auto-shrink a single-line label's font one baked step (16 -> 14 -> 12) until its
// text fits within max_w, so a longer translation (French/German/Russian/…) fits a
// fixed-width button or cell instead of clipping. No-op if it already fits or is
// already at 12 px. Cheap (one lv_txt_get_size per candidate) — call once, right
// after setting the label's text + font. Fonts scale on the Tanmatsu, so the ladder
// stays proportional there too. Wrapping/marquee is intentionally NOT used here —
// this keeps one-line button labels one line.
void uiFitLabelWidth(lv_obj_t* lbl, lv_coord_t max_w) {
  if (!lbl || max_w <= 6) return;
  const char* txt = lv_label_get_text(lbl);
  if (!txt || !txt[0]) return;
  const lv_font_t* cur = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
  lv_point_t sz;
  // Already fits at the current font? Do nothing. Measuring the CURRENT font first
  // (not assuming it's one of ours) means this never shrinks a label that fits — so
  // it's safe to call on any label, whatever font it inherited.
  lv_txt_get_size(&sz, txt, cur, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (sz.x <= max_w) return;
  // Overflows: pick the LARGEST baked font (16 -> 14 -> 12) that fits.
  const lv_font_t* ladder[3] = { &g_font_16, &g_font_14, &g_font_12 };
  for (int i = 0; i < 3; ++i) {
    lv_txt_get_size(&sz, txt, ladder[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= max_w) { lv_obj_set_style_text_font(lbl, ladder[i], LV_PART_MAIN); return; }
  }
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);   // still wide: 12 px beats clipping a bigger font
}
#if LV_USE_IMGFONT
// lv_imgfont path callback: hand back the baked colour image for an emoji
// codepoint (copied into the imgfont's scratch buffer as an lv_img_dsc_t), or
// false so LVGL keeps walking the fallback chain for everything else.
static bool emojiImgfontPathCb(const lv_font_t* /*font*/, void* img_src, uint16_t /*len*/,
                               uint32_t unicode, uint32_t /*unicode_next*/) {
  const lv_img_dsc_t* d = emojiGlyphLookup(unicode);
  if (!d) return false;
  lv_memcpy(img_src, d, sizeof(lv_img_dsc_t));
  // The imgfont reuses ONE scratch buffer as the image source for every glyph, so
  // LVGL's image cache (keyed on the src pointer) hands back the first-decoded
  // emoji for all of them — every emoji renders identical. Drop the stale entry so
  // the just-copied dsc is decoded fresh. (True-colour "decode" is a no-op copy, so
  // this is cheap, and other images — map tiles — use distinct src ptrs, untouched.)
  lv_img_cache_invalidate_src(img_src);
  return true;
}
static lv_font_t* s_emoji_font[3] = { nullptr, nullptr, nullptr };  // one per text size
#endif

// UI size (font-based, at native resolution so it stays crisp) is set in
// initTouchFontFallbacks() from the saved preset. SC() scales geometry where a board supports it. The bottom
// tab bar is deliberately NOT scaled (g_font_tab stays 16 px + TABBAR_H is fixed) — by request it
// stays the Normal size at every level.
static int s_ui_fscale = 100;
lv_coord_t SC(int px) { return (lv_coord_t)((px * s_ui_fscale + 50) / 100); }
// Popup-card dimension scaler: the 800×480 Tanmatsu panel dwarfs dialogs sized for the
// 320px T-Deck/V4 — scale their fixed card/menu W/H up so they don't look lost in the middle.
// No-op (plain SC) on the smaller boards, so their popups stay byte-for-byte unchanged.
// EXCLUDES the round P4: it's CAP_LARGE_SCREEN (for font scaling) but only 284 px wide, so
// the 1.7× would blow fixed-width cards (e.g. PSC(232)=394) far past the screen and clip the
// controls off-side. Plain SC keeps those cards ≤232 px — a comfortable fit on the 284 px panel.
#if CAP_LARGE_SCREEN && !CAP_ROUND_CORNERS
lv_coord_t PSC(int px) { return (lv_coord_t)((SC(px) * 17) / 10); }   // ~1.7× — wide Tanmatsu only
#else
lv_coord_t PSC(int px) { return SC(px); }
#endif
// Popup CARD WIDTH — PSC-scaled but clamped so a fixed-width dialog can never exceed the screen.
// On the narrow round P4 (284 px logical) a Large/Huge font scale pushes PSC(232) to ~325-394 px,
// which clipped settings cards off BOTH edges. The clamp keeps a small margin either side; it's a
// no-op on the wide boards (their PSC widths already fit). Height is unaffected (tall screen).
lv_coord_t PCW(int px) {
  lv_coord_t w = PSC(px);
  lv_coord_t cap = lv_disp_get_hor_res(nullptr) - SC(12);
  return (cap > 0 && w > cap) ? cap : w;
}
#if CAP_UI_SIZE
static lv_font_t g_font_tab;     // fixed 16 px tab-bar icon font (montserrat_16 + person glyph)
#endif

// Compact UI chrome (tab icons, popup close affordances, overview actions) must
// not grow with the semantic text preset. On the Pager's 222-px-tall panel a
// Large-preset 24-px glyph inside fixed 28/32-px controls crowds neighbouring
// content even though the control itself has not grown. Match the deliberately
// fixed bottom toolbar wherever UI-size presets exist.
const lv_font_t* uiChromeFont() {
#if CAP_UI_SIZE
  return &g_font_tab;
#else
  return &g_font_16;
#endif
}

// Jumbo is deliberately the Large UI with only conversation text taken to
// the Pager's 24-px role. Keeping this separate avoids enlarging status chrome,
// settings, timestamps, sender labels, and the composer on the Pager's short
// display. Compact-chat rows use the same role because the message and metadata
// share a single LVGL label there.
const lv_font_t* chatMessageFont() {
#if defined(TLORA_PAGER)
  return touchPrefsGetUiScale() == 3 ? &g_font_16 : &g_font_12;
#else
  return &g_font_12;
#endif
}

void initTouchFontFallbacks() {
#if defined(TLORA_PAGER)
  // The Pager is wide but only 222 px tall. Grow the semantic text roles while
  // leaving SC() at 100%; globally scaling every row/card made content
  // unreachable. Layouts that need more room are bounded individually below.
  s_ui_fscale = 100;
  switch (touchPrefsGetUiScale()) {
    case 1:
      g_font_12 = lv_font_montserrat_16;
      g_font_14 = lv_font_montserrat_18;
      g_font_16 = lv_font_montserrat_20;
      break;
    case 2:
    case 3:   // Jumbo keeps Large chrome; chat message text is bumped separately.
      g_font_12 = lv_font_montserrat_18;
      g_font_14 = lv_font_montserrat_20;
      g_font_16 = lv_font_montserrat_24;
      break;
    default:
      g_font_12 = lv_font_montserrat_12;
      g_font_14 = lv_font_montserrat_14;
      g_font_16 = lv_font_montserrat_16;
      break;
  }
  g_font_tab = lv_font_montserrat_16;
#elif CAP_LARGE_SCREEN
  // Crisp "UI size": render bigger by swapping in larger built-in Montserrat fonts (NOT by
  // upscaling a low-res frame). g_font_12/14/16 are what the whole UI draws with, so this scales
  // every screen at once. The colour-emoji + non-Latin fallbacks stay their baked sizes (they don't
  // grow), which is fine for Latin text. g_font_tab is pinned to 16 px so the bottom bar never grows.
  // UI scale from the saved pref (Normal/Large/Huge -> 100/140/170%). The P4 honours it like the
  // Tanmatsu — its "UI size" dropdown is shown (CAP_LARGE_SCREEN), so pinning 100% here made that
  // setting a dead no-op (reported: "changing text size doesn't work"). At Large/Huge some P4 chrome
  // that still uses unscaled dims (status bar, home grid, list rows) can be tight; that is the lesser
  // problem and a per-screen SC() follow-up, not a reason to disable scaling outright.
  switch (touchPrefsGetUiScale()) { case 1: s_ui_fscale = 140; break; case 2: s_ui_fscale = 170; break; default: s_ui_fscale = 100; break; }
  switch (s_ui_fscale) {
    case 140: g_font_12 = lv_font_montserrat_16; g_font_14 = lv_font_montserrat_20; g_font_16 = lv_font_montserrat_24; break;  // Large ~1.4x
    case 170: g_font_12 = lv_font_montserrat_20; g_font_14 = lv_font_montserrat_24; g_font_16 = lv_font_montserrat_28; break;  // Huge  ~1.7x
    default:  g_font_12 = lv_font_montserrat_12; g_font_14 = lv_font_montserrat_14; g_font_16 = lv_font_montserrat_16; break;  // Normal
  }
  g_font_tab = lv_font_montserrat_16;
#else
  g_font_12 = lv_font_montserrat_12;
  g_font_14 = lv_font_montserrat_14;
  g_font_16 = lv_font_montserrat_16;
#endif
#if LV_USE_IMGFONT
  // Insert the colour-emoji image font as the tail of each chain:
  //   g_font_NN (montserrat, Latin) -> emoji (colour images) -> extras_NN
  //   (Cyrillic/Greek/Arabic). The emoji font returns false for non-emoji
  //   codepoints, so they fall straight through to the size-matched extras.
  const lv_font_t* extras[3] = { &extras_12, &extras_14, &extras_16 };
#if defined(TLORA_PAGER)
  switch (touchPrefsGetUiScale()) {
    case 1: extras[0] = &extras_16; extras[1] = &extras_20; extras[2] = &extras_20; break;
    case 2:
    case 3: extras[0] = &extras_20; extras[1] = &extras_20; extras[2] = &extras_24; break;
    default: break;
  }
#endif
#if defined(HAS_TANMATSU)
  // At Large/Huge scale the primaries become Montserrat 20/24/28 (ASCII-only),
  // so accents dropped to the 16 px fallback and looked tiny. Splice the
  // size-matched Latin-accent font per slot; tail is extras_16 so non-Latin
  // still resolves. g_font sizes: Large(140) 16/20/24, Huge(170) 20/24/28.
  static lv_font_t s_acc_scaled[3];
  const lv_font_t* acc[3] = { nullptr, nullptr, nullptr };
  if (s_ui_fscale == 140)      { acc[1] = &extras_lat_20; acc[2] = &extras_lat_24; }
  else if (s_ui_fscale == 170) { acc[0] = &extras_lat_20; acc[1] = &extras_lat_24; acc[2] = &extras_lat_28; }
  for (int i = 0; i < 3; ++i) {
    if (!acc[i]) continue;
    s_acc_scaled[i] = *acc[i];
    s_acc_scaled[i].fallback = &extras_16;
    extras[i] = &s_acc_scaled[i];
  }
#endif
  lv_font_t*       prim[3]   = { &g_font_12, &g_font_14, &g_font_16 };
#if defined(TLORA_PAGER)
  // LVGL lays out every glyph in a fallback chain using the primary font's
  // line box. The generated OFL fallbacks need a taller box than Montserrat at
  // the Pager's accessible sizes, so carry those metrics onto the primary
  // copies. Without this, non-Latin and accented glyphs can clip and multiline
  // input grows by too little even though the fallback bitmap itself is the
  // requested size.
  if (touchPrefsGetUiScale() != 0) {
    for (int i = 0; i < 3; ++i) {
      if (extras[i]->line_height > prim[i]->line_height) {
        prim[i]->line_height = extras[i]->line_height;
        prim[i]->base_line = extras[i]->base_line;
      }
    }
  }
#endif
  for (int i = 0; i < 3; ++i) {
    if (!s_emoji_font[i]) s_emoji_font[i] = lv_imgfont_create(16, emojiImgfontPathCb);   // 16 px baked glyphs (~15% larger; sit on the text baseline)
    if (s_emoji_font[i]) { s_emoji_font[i]->fallback = extras[i]; prim[i]->fallback = s_emoji_font[i]; }
    else                 { prim[i]->fallback = extras[i]; }        // OOM: plain chain
  }
#else
  g_font_12.fallback = &extras_12;
  g_font_14.fallback = &extras_14;
  g_font_16.fallback = &extras_16;
#endif
  // Contacts-tab person glyph: splice onto the HEAD of g_font_16's chain — the
  // tab bar's btnmatrix renders its icons in g_font_16 (g_font_16 -> person ->
  // [emoji ->] extras). One PUA codepoint (U+F007); it follows the tab's
  // active/inactive text colour like the other monochrome tab symbols.
  static lv_font_t s_person_font;
  s_person_font = person_font;
  s_person_font.fallback = g_font_16.fallback;
  g_font_16.fallback = &s_person_font;
  // Control-center glyphs (lock / bell / bell-slash): splice onto the HEAD of
  // g_font_16's chain too, so the CC chips (which render their icon in g_font_16)
  // resolve U+F023/F0F3/F1F6. Chain: montserrat_16 -> cc_icons -> person -> [emoji] -> extras.
  static lv_font_t s_cc_icons_font;
  s_cc_icons_font = cc_icons_16;
  s_cc_icons_font.fallback = g_font_16.fallback;
  g_font_16.fallback = &s_cc_icons_font;
#if CAP_UI_SIZE
  // The fixed-size tab bar font needs the person glyph too (Contacts tab icon, U+F007) at 16 px.
  static lv_font_t s_tab_person; s_tab_person = person_font; s_tab_person.fallback = nullptr;
  g_font_tab.fallback = &s_tab_person;
#endif
  // Map zoom magnifier (U+F002) — head of the g_font_16 chain (overlay buttons
  // render in g_font_16). One PUA codepoint; misses on it are free for plain text.
  static lv_font_t s_zoom_font;
  s_zoom_font = zoom_font;
  s_zoom_font.fallback = g_font_16.fallback;
  g_font_16.fallback = &s_zoom_font;
  // Sleep-state icons: sun (U+F185) + moon (U+F186). Spliced into all three
  // chains — g_font_12 for the status-bar sleep indicator, g_font_14 for the
  // battery-chart sleep markers, g_font_16 for any larger context. A single font
  // object can only sit in one chain at a time; use distinct static copies.
  static lv_font_t s_sleep_font_12;
  s_sleep_font_12 = sleepicons_font;
  s_sleep_font_12.fallback = g_font_12.fallback;
  g_font_12.fallback = &s_sleep_font_12;
  static lv_font_t s_sleep_font_14;
  s_sleep_font_14 = sleepicons_font;
  s_sleep_font_14.fallback = g_font_14.fallback;
  g_font_14.fallback = &s_sleep_font_14;
  static lv_font_t s_sleep_font_16;
  s_sleep_font_16 = sleepicons_font;
  s_sleep_font_16.fallback = g_font_16.fallback;
  g_font_16.fallback = &s_sleep_font_16;
  // Also reach the person/antenna/group glyphs from g_font_14 — the contact
  // action sheet title and the Chats-list rows render the icon inline with 14 px
  // text. Use the 14 px-rendered person_font14 (NOT the 16 px person_font) so the
  // glyph matches the line height instead of overshooting + clipping at the top.
  // fallback is only consulted on a glyph miss, so normal 14 px text pays nothing.
  static lv_font_t s_person_font14;
  s_person_font14 = person_font14;
  s_person_font14.fallback = g_font_14.fallback;
  g_font_14.fallback = &s_person_font14;
}


const lv_font_t& font12() { return g_font_12; }
const lv_font_t& font14() { return g_font_14; }
const lv_font_t& font16() { return g_font_16; }
const lv_font_t* emojiFont() {
#if LV_USE_IMGFONT
  return s_emoji_font[2];
#else
  return nullptr;
#endif
}
} }
