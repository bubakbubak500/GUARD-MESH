// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "theme/Fonts.h"
#include "screens/ContactActionSheet.h"
#include "screens/TextEditMenu.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

void runCzechLayoutRegression(void (*save)(const char *)) {
  using namespace ui::theme;
  // T-Deck contact grid: 232px card, 6px padding/gap, 8px label inset.
  // Include the icon and spaces: counting plain characters misses their width.
  struct Caption { const char *key; const char *prefix; };
  const Caption contact[] = {
      {"Message", LV_SYMBOL_ENVELOPE "  "}, {"Open chat", LV_SYMBOL_ENVELOPE "  "},
      {"Join", LV_SYMBOL_OK "  "}, {"Telemetry", LV_SYMBOL_BATTERY_3 "  "},
      {"Show on map", LV_SYMBOL_GPS "  "}, {"Range test", LV_SYMBOL_WIFI "  "},
      {"Sightline", LV_SYMBOL_GPS "  "}, {"Favorite", "*  "}, {"Unfav", "*  "},
      {"Share my loc", LV_SYMBOL_GPS "  "}, {"Stop sharing loc", LV_SYMBOL_GPS "  "},
      {"Reset path", LV_SYMBOL_LOOP "  "}, {"Share contact", LV_SYMBOL_UPLOAD "  "},
      {"Block", LV_SYMBOL_CLOSE "  "}, {"Unblock", LV_SYMBOL_OK "  "},
  };
  bool fits = true;
  for (const auto &caption : contact) {
    char text[120];
    snprintf(text, sizeof text, "%s%s", caption.prefix, TR(caption.key));
    lv_point_t size;
    lv_txt_get_size(&size, text, &font12(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    printf("compact contact: %s = %d / 99 px\n", caption.key, size.x);
    if (i18nGetLang() == LANG_CS && size.x > 99)
      fits = false;
  }
  if (!fits)
    throw std::runtime_error("Contact action translation exceeds 99px label");
  puts(i18nGetLang() == LANG_CS ? "Compact Czech contact labels: real LVGL font widths PASS"
                              : "English contact label width baseline recorded");
  ui::screens::ContactActionSheet::Host host;
  host.contentTop = []() -> lv_coord_t { return 24; };
  host.closeRoot = [](void *, lv_obj_t **root) {
    auto *old = *root;
    *root = nullptr;
    lv_obj_del(old);
  };
  ui::screens::ContactActionSheet sheet(host);
  ui::screens::ContactActionSheet::Snapshot snapshot;
  std::strcpy(snapshot.displayName, "SIM Alpha");
  snapshot.hasMap = snapshot.hasSightline = snapshot.canShareLocation = snapshot.favorite = true;
  sheet.open(snapshot);
  if (save) save("compact-contact-actions.png");
  sheet.close();
  auto *field = lv_textarea_create(lv_layer_top());
  lv_obj_set_pos(field, 16, 100);
  lv_obj_set_size(field, 280, 34);
  lv_textarea_set_text(field, "Příliš žluťoučký kůň");
  ui::screens::textEditMenu::show(field);
  if (save) save("compact-text-actions.png");
  ui::screens::textEditMenu::close();
  lv_obj_del(field);
}
