#include "ProfileSettingsScreen.h"
#include "../platform/UiDevice.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/FormFields.h"
using namespace ui::widgets;
namespace ui { namespace screens {
using namespace theme;
void ProfileSettingsScreen::build(lv_obj_t* body, lv_coord_t width) {
  // No "Profile" group header — it just duplicates the sub-tab button name.
  adopt(body, width);
  if (!body) return;
  int y = 0;
  const lv_coord_t cw = _width;
  auto mk_label = [&](const char* text) {
    y += settingsRowLabel(body, y, 0, text, colors().COLOR_SUB, nullptr, 0) + 2;
  };
  auto mk_ta = [&](int w, int x, const char* ph, int max_len) -> lv_obj_t* {
    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, w, SC(30));
    lv_obj_set_pos(ta, x, y);
    lv_textarea_set_one_line(ta, true);
    taSetPlaceholder(ta, TR(ph));
    lv_textarea_set_max_length(ta, max_len);
    _host.attachField(ta);
    return ta;
  };

  mk_label(TR("Node name"));
  _fields.name_ta = mk_ta(cw, 0, TR("Node name"), 31);
  lv_obj_add_event_cb(_fields.name_ta, +[](lv_event_t* event){ static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(event))->saveProfileNameCb(event); }, LV_EVENT_DEFOCUSED, this);  // auto-save on blur
  if (_host.task()) {
    char nm_vis[40];
    const char* raw = _host.task()->getNodeNameCstr();
    _host.filterText(&font14(), nm_vis, sizeof(nm_vis), raw ? raw : "");
    lv_textarea_set_text(_fields.name_ta, nm_vis);
  }
  y += SC(36);
  mk_label(TR("Advert location"));
  const int loc_g = 8, loc_fw = (cw - loc_g) / 2;   // lat / lon fill the row evenly
  _fields.lat_ta = mk_ta(loc_fw, 0, TR("Latitude"), 20);
  _fields.lon_ta = mk_ta(loc_fw, loc_fw + loc_g, TR("Longitude"), 20);
  lv_obj_add_event_cb(_fields.lat_ta, +[](lv_event_t* event){ static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(event))->saveProfilePosCb(event); }, LV_EVENT_DEFOCUSED, this);  // auto-save the pair on blur
  lv_obj_add_event_cb(_fields.lon_ta, +[](lv_event_t* event){ static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(event))->saveProfilePosCb(event); }, LV_EVENT_DEFOCUSED, this);
  if (_host.task()) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.6f", _host.task()->getNodeLat());
    lv_textarea_set_text(_fields.lat_ta, buf);
    snprintf(buf, sizeof(buf), "%.6f", _host.task()->getNodeLon());
    lv_textarea_set_text(_fields.lon_ta, buf);
  }
  y += SC(40);
  // No "Save name" / "Save position" buttons — both auto-save on blur.

  {
    auto mk_label = [&](const char* text) {
      y += settingsRowLabel(body, y, 0, text, colors().COLOR_SUB, nullptr, 0) + 2;
    };
    auto mk_switch = [&](const char* text, lv_obj_t** out) {
      int h = settingsRowLabel(body, y, 6, text, colors().COLOR_SUB, nullptr, 56);
      lv_obj_t* sw = lv_switch_create(body);
      lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
      if (out) *out = sw;
      y += LV_MAX(34, h + 12);
    };

    // (Path-hash size moved to Radio settings as "Multi-byte routing" — it's the
    // same NodePrefs.path_hash_mode, so no duplicate control here.)
    mk_label(TR("Advert"));
    mk_switch(TR("Share location in advert"), &_fields.share_loc_sw);
    NodePrefs* pol_prefs = the_mesh.getNodePrefs();
    if (pol_prefs && pol_prefs->advert_loc_policy)
      lv_obj_add_state(_fields.share_loc_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(_fields.share_loc_sw, +[](lv_event_t* event){ static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(event))->savePolicyCb(event); }, LV_EVENT_VALUE_CHANGED, this);  // instant apply — no Save button
  }

  // ---- Identity + Share QR (moved to the BOTTOM of the Profile page) ----
  // Public key — long-press copies. We don't surface the *private* key here
  // (LocalIdentity::prv_key is library-private, and private material on a touch
  // screen is a footgun).
  mk_label(TR("Identity (public key)"));
  {
    const uint8_t* pk = the_mesh.getSelfPubKey();
    char pk_hex[2 * PUB_KEY_SIZE + 1];
    for (int i = 0; i < (int)PUB_KEY_SIZE; ++i) {
      snprintf(pk_hex + i*2, 3, "%02x", pk[i]);
    }
    lv_obj_t* pk_lbl = lv_label_create(body);
    lv_label_set_long_mode(pk_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(pk_lbl, lv_pct(100));
    lv_label_set_text(pk_lbl, pk_hex);
    lv_obj_set_style_text_color(pk_lbl, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(pk_lbl, &font12(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pk_lbl, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pk_lbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pk_lbl, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(pk_lbl, 4, LV_PART_MAIN);
    lv_obj_set_pos(pk_lbl, 2, y);
    lv_obj_add_flag(pk_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pk_lbl, _host.copyLabel, LV_EVENT_LONG_PRESSED,
                        const_cast<char*>("pubkey"));
    y += SC(50);
  }

  // "Share QR" button — second entry point to the same popup as the Chats header.
  {
    lv_obj_t* sb = lv_btn_create(body);
    lv_obj_set_size(sb, lv_pct(100),SC(34));
    lv_obj_set_pos(sb, 2, y);
    styleButton(sb);
    lv_obj_add_event_cb(sb, +[](lv_event_t* e) {
      auto* self = static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(e));
      if (!self->accepts(e)) return;
      auto& _host = self->_host;
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      _host.share();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* sl = lv_label_create(sb);
    lv_label_set_text(sl, TR(LV_SYMBOL_IMAGE "   Share QR"));
    lv_obj_set_style_text_color(sl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(sl, &font14(), LV_PART_MAIN);
    lv_obj_center(sl);
    y += SC(42);
  }

  // "Export settings" — write a MeshCore-app-compatible JSON backup (identity,
  // radio/position, channels, contacts) to SD if a card is in, else internal
  // flash. The file opens in the stock app / web client.
  {
    lv_obj_t* eb = lv_btn_create(body);
    lv_obj_set_size(eb, lv_pct(100), SC(34));
    lv_obj_set_pos(eb, 2, y);
    styleButton(eb);
    lv_obj_add_event_cb(eb, +[](lv_event_t* e) {
      auto* self = static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(e));
      if (!self->accepts(e)) return;
      auto& _host = self->_host;
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      // Fixed, app-compatible name so the stock app / web client can read it back.
      _host.exportBackup("meshcore-backup.json");
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* el = lv_label_create(eb);
    lv_label_set_text(el, TR("Export settings"));
    lv_obj_set_style_text_color(el, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(el, &font14(), LV_PART_MAIN);
    lv_obj_center(el);
    y += SC(42);
  }

  // "Import settings" — restore a MeshCore-app-compatible JSON backup from
  // /meshcore-backup.json (SD if present, else internal). Replaces identity,
  // channels and contacts, then reboots so radio settings take effect.
  {
    lv_obj_t* ib = lv_btn_create(body);
    lv_obj_set_size(ib, lv_pct(100), SC(34));
    lv_obj_set_pos(ib, 2, y);
    styleButton(ib);
    lv_obj_add_event_cb(ib, +[](lv_event_t* e) {
      auto* self = static_cast<ProfileSettingsScreen*>(lv_event_get_user_data(e));
      if (!self->accepts(e)) return;
      auto& _host = self->_host;
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      _host.importBackup();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* il = lv_label_create(ib);
    lv_label_set_text(il, TR("Import settings"));
    lv_obj_set_style_text_color(il, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(il, &font14(), LV_PART_MAIN);
    lv_obj_center(il);
    y += SC(42);
  }
}

void ProfileSettingsScreen::saveProfileNameCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (_host.deleting(e)) return;   // the widget is being destroyed
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_DEFOCUSED) || !_host.task() || !_fields.name_ta) return;
  _host.syncKeyboard();
  const char* name = lv_textarea_get_text(_fields.name_ta);
  if (_host.task()->setNodeName(name)) {
    _host.task()->showAlert(TR("Name saved"), 1000);
    _host.refresh();
  } else {
    _host.task()->showAlert(TR("Couldn't save the name to storage"), 2200);
  }
}

void ProfileSettingsScreen::saveProfilePosCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (_host.deleting(e)) return;   // the widget is being destroyed
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_DEFOCUSED) || !_host.task()) return;
  const bool silent = (_c == LV_EVENT_DEFOCUSED);   // blur auto-save: quiet if mid-edit
  _host.syncKeyboard();
  float lat = 0.0f, lon = 0.0f;
  if (!parseFloatField(_fields.lat_ta, lat) || !parseFloatField(_fields.lon_ta, lon)) {
    if (!silent) _host.task()->showAlert(TR("Invalid lat/lon"), 1200);
    return;
  }
  if (_host.task()->setPosition(static_cast<double>(lat), static_cast<double>(lon))) {
    _host.task()->showAlert(TR("Position saved"), 1000);
    _host.refresh();
  }
}

void ProfileSettingsScreen::savePolicyCb(lv_event_t* e) {
  if (!accepts(e)) return;
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_VALUE_CHANGED) || !_host.task()) return;
  uint8_t share = (_fields.share_loc_sw && lv_obj_has_state(_fields.share_loc_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  _host.task()->setAdvertLocationPolicy(share);   // applies instantly on toggle
  _host.refresh();
}
} }
