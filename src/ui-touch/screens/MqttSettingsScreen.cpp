// SPDX-License-Identifier: GPL-3.0-or-later
#include "MqttSettingsScreen.h"
#include "../platform/UiDevice.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
namespace ui { namespace screens {
using namespace theme;
using namespace widgets;
void MqttSettingsScreen::build(lv_obj_t* body, lv_coord_t width) {
  adopt(body, width);
  if (!body) return;
  const lv_coord_t cw = _width;
  int y = 0;

  // ---- Privacy warning (read before enabling) ----
  lv_obj_t* warn = lv_label_create(body);
  lv_label_set_text(warn, TR("Highly experimental. This forwards the text, sender name and timestamp of every message your node receives to an MQTT broker, where anyone able to read the broker can read them. Direct messages are private messages from other people who never agreed to be shared. Use a broker you control, set an encryption key below, and never a public broker."));
  lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(warn, cw);
  lv_obj_set_style_text_color(warn, lightSurfaceTextColor(0xCC6A00), LV_PART_MAIN);
  lv_obj_set_style_text_font(warn, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(warn, 2, y);
  lv_obj_update_layout(warn);
  y += lv_obj_get_height(warn) + SC(8);

  // ---- Consent checkbox (must be ticked to unlock the enable switch) ----
  _fields.mqtt_consent_cb = lv_checkbox_create(body);
  lv_checkbox_set_text(_fields.mqtt_consent_cb, TR("I accept the privacy risk"));
  lv_obj_set_width(_fields.mqtt_consent_cb, cw);
  // High-contrast label + a clearly outlined box. It was inheriting the dim default
  // checkbox text colour (every other checkbox sets one), which made the consent
  // line next to the box hard to read.
  lv_obj_set_style_text_color(_fields.mqtt_consent_cb, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(_fields.mqtt_consent_cb, &font14(), LV_PART_MAIN);
  lv_obj_set_style_radius(_fields.mqtt_consent_cb, 3, LV_PART_INDICATOR);
  lv_obj_set_style_border_color(_fields.mqtt_consent_cb, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_border_width(_fields.mqtt_consent_cb, 1, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(_fields.mqtt_consent_cb, lv_color_hex(0x4F9DF7), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_pos(_fields.mqtt_consent_cb, 2, y);
  lv_obj_add_event_cb(_fields.mqtt_consent_cb, +[](lv_event_t* event){ static_cast<MqttSettingsScreen*>(lv_event_get_user_data(event))->mqttConsentCb(event); }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_update_layout(_fields.mqtt_consent_cb);
  y += lv_obj_get_height(_fields.mqtt_consent_cb) + SC(10);

  // ---- Enable toggle (disabled until consent is given) ----
  lv_obj_t* en_lbl = lv_label_create(body);
  lv_label_set_text(en_lbl, TR("Enable MQTT bridge"));
  lv_obj_set_style_text_color(en_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(en_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(en_lbl, 2, y + 8);
  _fields.mqtt_en_sw = lv_switch_create(body);
  lv_obj_align(_fields.mqtt_en_sw, LV_ALIGN_TOP_RIGHT, 0, y);
  lv_obj_add_state(_fields.mqtt_en_sw, LV_STATE_DISABLED);   // unlocked by the consent checkbox
  y += SC(38);

  // ---- Broker host ----
  lv_obj_t* host_lbl = lv_label_create(body);
  lv_label_set_text(host_lbl, TR("Broker host / IP"));
  lv_obj_set_style_text_color(host_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(host_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(host_lbl, 2, y);
  y += SC(16);
  _fields.mqtt_host_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.mqtt_host_ta, cw - 86, SC(30));
  lv_obj_set_pos(_fields.mqtt_host_ta, 0, y);
  lv_textarea_set_one_line(_fields.mqtt_host_ta, true);
  taSetPlaceholder(_fields.mqtt_host_ta, "192.168.1.x");
  lv_textarea_set_max_length(_fields.mqtt_host_ta, 63);
  _host.attachField(_fields.mqtt_host_ta);
  // Port field on the same row
  lv_obj_t* port_lbl = lv_label_create(body);
  lv_label_set_text(port_lbl, TR("Port"));
  lv_obj_set_style_text_color(port_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(port_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(port_lbl, cw - 82, y - SC(16));
  _fields.mqtt_port_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.mqtt_port_ta, 80, SC(30));
  lv_obj_set_pos(_fields.mqtt_port_ta, cw - 80, y);
  lv_textarea_set_one_line(_fields.mqtt_port_ta, true);
  taSetPlaceholder(_fields.mqtt_port_ta, "1883");
  lv_textarea_set_max_length(_fields.mqtt_port_ta, 5);
  lv_textarea_set_accepted_chars(_fields.mqtt_port_ta, "0123456789");
  _host.attachField(_fields.mqtt_port_ta);
  y += SC(36);

  // ---- Username (optional) ----
  lv_obj_t* user_lbl = lv_label_create(body);
  lv_label_set_text(user_lbl, TR("Username (optional)"));
  lv_obj_set_style_text_color(user_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(user_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(user_lbl, 2, y);
  y += SC(16);
  _fields.mqtt_user_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.mqtt_user_ta, lv_pct(100), SC(30));
  lv_obj_set_pos(_fields.mqtt_user_ta, 0, y);
  lv_textarea_set_one_line(_fields.mqtt_user_ta, true);
  taSetPlaceholder(_fields.mqtt_user_ta, TR("Leave empty if not required"));
  lv_textarea_set_max_length(_fields.mqtt_user_ta, 31);
  _host.attachField(_fields.mqtt_user_ta);
  y += SC(36);

  // ---- Password (optional) ----
  lv_obj_t* pwd_lbl = lv_label_create(body);
  lv_label_set_text(pwd_lbl, TR("Password (optional)"));
  lv_obj_set_style_text_color(pwd_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(pwd_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(pwd_lbl, 2, y);
  y += SC(16);
  _fields.mqtt_pwd_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.mqtt_pwd_ta, lv_pct(100), SC(30));
  lv_obj_set_pos(_fields.mqtt_pwd_ta, 0, y);
  lv_textarea_set_one_line(_fields.mqtt_pwd_ta, true);
  lv_textarea_set_password_mode(_fields.mqtt_pwd_ta, true);
  taSetPlaceholder(_fields.mqtt_pwd_ta, TR("Leave empty if not required"));
  lv_textarea_set_max_length(_fields.mqtt_pwd_ta, 31);
  _host.attachField(_fields.mqtt_pwd_ta);
  _host.attachSymbols(_fields.mqtt_pwd_ta);
  y += SC(36);

  // ---- Publish toggles: channel on by default, DMs opt-in ----
  lv_obj_t* ch_lbl = lv_label_create(body);
  lv_label_set_text(ch_lbl, TR("Publish channel messages"));
  lv_obj_set_style_text_color(ch_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(ch_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(ch_lbl, 2, y + 8);
  _fields.mqtt_ch_sw = lv_switch_create(body);
  lv_obj_align(_fields.mqtt_ch_sw, LV_ALIGN_TOP_RIGHT, 0, y);
  y += SC(38);

  lv_obj_t* dm_lbl = lv_label_create(body);
  lv_label_set_text(dm_lbl, TR("Publish direct messages"));
  lv_obj_set_style_text_color(dm_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(dm_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(dm_lbl, 2, y + 8);
  _fields.mqtt_dm_sw = lv_switch_create(body);
  lv_obj_align(_fields.mqtt_dm_sw, LV_ALIGN_TOP_RIGHT, 0, y);
  y += SC(38);

  // ---- Encryption key (PSK): seals payloads with AES-GCM; empty = plaintext ----
  lv_obj_t* psk_lbl = lv_label_create(body);
  lv_label_set_text(psk_lbl, TR("Encryption key (optional)"));
  lv_obj_set_style_text_color(psk_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(psk_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(psk_lbl, 2, y);
  y += SC(16);
  _fields.mqtt_psk_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.mqtt_psk_ta, lv_pct(100), SC(30));
  lv_obj_set_pos(_fields.mqtt_psk_ta, 0, y);
  lv_textarea_set_one_line(_fields.mqtt_psk_ta, true);
  lv_textarea_set_password_mode(_fields.mqtt_psk_ta, true);
  taSetPlaceholder(_fields.mqtt_psk_ta, TR("Empty = plaintext to a private broker"));
  lv_textarea_set_max_length(_fields.mqtt_psk_ta, 32);
  _host.attachField(_fields.mqtt_psk_ta);
  _host.attachSymbols(_fields.mqtt_psk_ta);
  y += SC(36);

  // ---- Load current config ----
  {
    SdNvsPrefs p;   // file-backed, matches MqttBridge (GH #128)
    bool cur_en = false, cur_dm = false, cur_ch = true, cur_consent = false;
    char cur_host[64] = {}, cur_port_s[8] = "1883", cur_user[32] = {}, cur_pwd[32] = {}, cur_psk[33] = {};
    if (p.begin("mqtt", true)) {
      cur_en = p.getBool("en", false);
      cur_dm = p.getBool("dm", false);
      cur_ch = p.getBool("ch", true);
      cur_consent = p.getBool("consent", false);
      uint16_t port = (uint16_t)p.getUInt("port", 1883);
      snprintf(cur_port_s, sizeof(cur_port_s), "%u", port);
      if (p.isKey("host")) p.getString("host", cur_host, sizeof(cur_host));
      if (p.isKey("user")) p.getString("user", cur_user, sizeof(cur_user));
      if (p.isKey("pwd"))  p.getString("pwd",  cur_pwd,  sizeof(cur_pwd));
      if (p.isKey("psk"))  p.getString("psk",  cur_psk,  sizeof(cur_psk));
      p.end();
    }
    if (cur_consent) {
      lv_obj_add_state(_fields.mqtt_consent_cb, LV_STATE_CHECKED);
      lv_obj_clear_state(_fields.mqtt_en_sw, LV_STATE_DISABLED);   // consent already given → unlock
    }
    if (cur_en && cur_consent) lv_obj_add_state(_fields.mqtt_en_sw, LV_STATE_CHECKED);
    if (cur_ch) lv_obj_add_state(_fields.mqtt_ch_sw, LV_STATE_CHECKED);
    if (cur_dm) lv_obj_add_state(_fields.mqtt_dm_sw, LV_STATE_CHECKED);
    lv_textarea_set_text(_fields.mqtt_host_ta, cur_host);
    lv_textarea_set_text(_fields.mqtt_port_ta, cur_port_s);
    lv_textarea_set_text(_fields.mqtt_user_ta, cur_user);
    lv_textarea_set_text(_fields.mqtt_pwd_ta,  cur_pwd);
    lv_textarea_set_text(_fields.mqtt_psk_ta,  cur_psk);
  }

  // ---- Save button ----
  lv_obj_t* b_save = lv_btn_create(body);
  lv_obj_set_size(b_save, lv_pct(100), SC(36));
  lv_obj_set_pos(b_save, 0, y);
  styleButton(b_save);
  lv_obj_add_event_cb(b_save, +[](lv_event_t* event){ static_cast<MqttSettingsScreen*>(lv_event_get_user_data(event))->mqttSaveCb(event); }, LV_EVENT_CLICKED, this);
  { lv_obj_t* sl = lv_label_create(b_save);
    lv_label_set_text(sl, TR(LV_SYMBOL_SAVE "  Save"));
    lv_obj_center(sl); }
  y += SC(44);

  lv_obj_set_height(body, y + SC(8));

}

void MqttSettingsScreen::mqttConsentCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (!_fields.mqtt_consent_cb || !_fields.mqtt_en_sw) return;
  bool ok = lv_obj_has_state(_fields.mqtt_consent_cb, LV_STATE_CHECKED);
  if (ok) {
    lv_obj_clear_state(_fields.mqtt_en_sw, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(_fields.mqtt_en_sw, LV_STATE_CHECKED);   // force off if consent withdrawn
    lv_obj_add_state(_fields.mqtt_en_sw, LV_STATE_DISABLED);
  }
}

void MqttSettingsScreen::mqttSaveCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!_fields.mqtt_host_ta) return;
  const char* host = lv_textarea_get_text(_fields.mqtt_host_ta);
  const char* portStr = lv_textarea_get_text(_fields.mqtt_port_ta);
  const char* user = lv_textarea_get_text(_fields.mqtt_user_ta);
  const char* pwd  = lv_textarea_get_text(_fields.mqtt_pwd_ta);
  uint16_t port = portStr && portStr[0] ? (uint16_t)atoi(portStr) : 1883;
  if (!port) port = 1883;
  const char* psk = _fields.mqtt_psk_ta ? lv_textarea_get_text(_fields.mqtt_psk_ta) : "";
  // Enable is only honoured when consent is ticked — belt-and-suspenders with the UI gating.
  bool consent = _fields.mqtt_consent_cb && lv_obj_has_state(_fields.mqtt_consent_cb, LV_STATE_CHECKED);
  bool en = consent && _fields.mqtt_en_sw && lv_obj_has_state(_fields.mqtt_en_sw, LV_STATE_CHECKED);
  bool pub_ch = !_fields.mqtt_ch_sw || lv_obj_has_state(_fields.mqtt_ch_sw, LV_STATE_CHECKED);
  bool pub_dm = _fields.mqtt_dm_sw && lv_obj_has_state(_fields.mqtt_dm_sw, LV_STATE_CHECKED);
  MqttBridge::saveConfig(host, port, user, pwd, pub_dm, pub_ch, psk, en);
  { SdNvsPrefs p; if (p.begin("mqtt", false)) { p.putBool("consent", consent); p.end(); } }   // file-backed, not NVS (GH #128)
  mqtt_bridge.reloadConfig();
  _host.closeModal();
}
} }
#endif
