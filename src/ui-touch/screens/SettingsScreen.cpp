#include "SettingsScreen.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../i18n.h"
namespace ui { namespace screens {
using namespace theme;
using namespace widgets;
void SettingsScreen::reset() {
  _root = _badge = nullptr;
  for (auto& card : _cards) card = nullptr;
}
void SettingsScreen::clear() {
  if (_root) {
    lv_obj_remove_event_cb_with_user_data(_root, deleteEvent, this);
    lv_obj_del(_root);
  }
  reset();
}
void SettingsScreen::deleteEvent(lv_event_t* event) {
  auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(event));
  if (self && self->_root == lv_event_get_target(event)) self->reset();
}
void SettingsScreen::hide(int category, bool hidden) {
  if (category < 0 || category >= MaxCategories || !_cards[category]) return;
  if (hidden) lv_obj_add_flag(_cards[category], LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(_cards[category], LV_OBJ_FLAG_HIDDEN);
}
void SettingsScreen::build(lv_obj_t* tab, const Category* categories, int count,
                           bool (*visible)(int), lv_event_cb_t open,
                           void (*scrollbar)(lv_obj_t*), int about) {
  clear();
  if (!tab || !categories || count < 0 || count > MaxCategories) return;
  styleSurface(tab, colors().COLOR_BG);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

  // Category landing: a single-column list in portrait, a 2-column grid in
  // landscape (uses the extra width). Each card opens a focused detail sheet.
  const bool landscape = lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr);
  const lv_coord_t hor = lv_disp_get_hor_res(nullptr);

  lv_obj_t* land = lv_obj_create(tab);
  _root = land;
  lv_obj_add_event_cb(land, deleteEvent, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(land);
  lv_obj_set_size(land, lv_pct(100), lv_pct(100));
  styleSurface(land, colors().COLOR_BG, 0);
  lv_obj_set_style_pad_all(land, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(land, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_column(land, 8, LV_PART_MAIN);
  lv_obj_set_scroll_dir(land, LV_DIR_VER);
  scrollbar(land);
  lv_obj_set_flex_flow(land, landscape ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);

  // hor - 16 (page pad) - 8 (right gutter so the rightmost card clears the scrollbar);
  // landscape subtracts another 8 for the inter-column gap, then halves.
  const lv_coord_t card_w = landscape ? (lv_coord_t)((hor - 24 - 8) / 2) : (lv_coord_t)(hor - 24);
  const lv_coord_t card_h = landscape ? 54 : 46;

  for (int c = 0; c < count; ++c) {
    if (visible && !visible(c)) continue;
    lv_obj_t* card = lv_btn_create(land);
    _cards[c] = card;      // remembered so hiding can apply live
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(themeRole(0x2A2E34, colors().COLOR_BORDER)), LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, open, LV_EVENT_CLICKED, (void*)(intptr_t)c);

    lv_obj_t* icon = lv_label_create(card);
    lv_label_set_text(icon, categories[c].icon);
    lv_obj_set_style_text_font(icon, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, TR(categories[c].label));
    lv_obj_set_style_text_font(lbl, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    // Category name sits visually CENTRED in the card (icon stays a left accent).
    // The label spans the card minus a symmetric margin (so the left accent icon
    // never overlaps the text) and centre-aligns within that box; wrap keeps a long
    // translated name (e.g. "Πληκτρολόγιο", "Schnellantworten") inside the card.
    // No right chevron — it fought the centred text and pulled the eye off-centre.
    // Fill the card to the RIGHT of the left accent icon and centre within that
    // region. The old symmetric 40-px margins left only ~64 px on the T-Deck's
    // 2-column landscape cards, so a single word ("Bluetooth") split mid-word and
    // "Radio & Mesh" wrapped to two lines. Using the full post-icon width (~100 px)
    // keeps every English category name on one line, unsplit; long translations
    // still wrap at word boundaries.
    lv_obj_set_width(lbl, card_w - 34 - 8);   // start just past the icon, small right margin
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 34, 0);

    if (c == about) {   // update-available dot rides on the About card
      _badge = lv_obj_create(card);
      lv_obj_remove_style_all(_badge);
      // Pure decoration: a bare lv_obj is CLICKABLE by default, and keypad-nav's
      // leaf rule then harvests the dot INSTEAD of the About button it sits on
      // (the button became "a container with a clickable child"), making About
      // unreachable by keyboard whenever the update badge shows.
      lv_obj_clear_flag(_badge, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(_badge, 9, 9);
      lv_obj_set_style_radius(_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(_badge, lv_color_hex(0xE2403A), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(_badge, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_align(_badge, LV_ALIGN_RIGHT_MID, landscape ? -12 : -32, 0);
      lv_obj_add_flag(_badge, LV_OBJ_FLAG_HIDDEN);
    }
  }

}

} }
