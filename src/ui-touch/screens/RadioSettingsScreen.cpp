// SPDX-License-Identifier: GPL-3.0-or-later
#include "RadioSettingsScreen.h"
#include "../platform/UiDevice.h"
#include "../platform/UiPlatform.h"
#include "../models/RadioPresets.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/FormFields.h"
namespace ui { namespace screens {
using namespace theme;
using namespace widgets;
using namespace radio;
void RadioSettingsScreen::build(lv_obj_t* body, lv_coord_t width) {
  // No "Radio" group header — it just duplicates the sub-tab button name.
  adopt(body, width);
  if (!body) return;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
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
    lv_obj_add_event_cb(ta, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->saveRadioParamsCb(event); }, LV_EVENT_DEFOCUSED, this);  // auto-save the group on blur (silent if mid-edit)
    return ta;
  };

  // Section divider + small header — groups this flat list into RADIO / MESH / SIGNAL.
  auto mk_section = [&](const char* title) {
    y += SC(6);
    lv_obj_t* sep = lv_obj_create(body);
    lv_obj_remove_style_all(sep);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sep, _width, 1);
    lv_obj_set_pos(sep, 0, y);
    lv_obj_set_style_bg_color(sep, lv_color_hex(themeRole(0x303438, colors().COLOR_BORDER)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t* sl = lv_label_create(body);
    lv_label_set_text(sl, TR(title));
    lv_obj_set_style_text_color(sl, lightSurfaceTextColor(0x8A929B), LV_PART_MAIN);
    lv_obj_set_style_text_font(sl, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(sl, 2, y + SC(7));
    y += SC(28);
  };

  // No "RADIO" divider here — it duplicated the sub-tab name; the page opens straight on
  // the preset picker.
  mk_label(TR("Community Preset"));
  {
    static const size_t PRESET_OPT_SZ = 1200;
    static char* preset_opt_buf = (char*)ui::platform::allocate(PRESET_OPT_SZ, true);   // lazy-PSRAM (frees 1.2 KB internal .bss)
    if (preset_opt_buf) {
      size_t o = 0;
      int nw = snprintf(preset_opt_buf + o, PRESET_OPT_SZ - o, "Custom (manual)");
      if (nw > 0) o += static_cast<size_t>(nw);
      for (size_t i = 0; i < k_mesh_radio_preset_count && o + 2 < PRESET_OPT_SZ; ++i) {
        preset_opt_buf[o++] = '\n';
        nw = snprintf(preset_opt_buf + o, PRESET_OPT_SZ - o, "%s", k_mesh_radio_presets[i].label);
        if (nw < 0) break;
        o += static_cast<size_t>(nw);
      }
      preset_opt_buf[PRESET_OPT_SZ - 1] = '\0';
    }

    _fields.radio_preset_dd = lv_dropdown_create(body);
    lv_obj_set_size(_fields.radio_preset_dd, lv_pct(100),SC(34));
    lv_obj_set_pos(_fields.radio_preset_dd, 2, y);
    lv_dropdown_set_options(_fields.radio_preset_dd, preset_opt_buf ? preset_opt_buf : "Custom (manual)");
    lv_obj_set_style_text_font(_fields.radio_preset_dd, &font12(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_fields.radio_preset_dd, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(_fields.radio_preset_dd, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(_fields.radio_preset_dd, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    /* Dropdown list (the popup once tapped) */
    lv_obj_t* preset_list = lv_dropdown_get_list(_fields.radio_preset_dd);
    lv_obj_set_style_bg_color(preset_list, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(preset_list, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(preset_list, &font12(), LV_PART_MAIN);
    lv_obj_add_event_cb(_fields.radio_preset_dd, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioPresetChangedCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_fields.radio_preset_dd, _host.clampDropdown, LV_EVENT_CLICKED, nullptr);
    y += SC(40);
  }

  const lv_coord_t cw = _width;
  mk_label(TR("Frequency / bandwidth"));
  {
    const int g = 8, fw = (cw - g) / 2;           // two equal fields filling the row
    _fields.freq_ta = mk_ta(fw, 0, TR("MHz"), 15);
    _fields.bw_ta   = mk_ta(fw, fw + g, TR("kHz"), 10);
  }
  y += SC(44);   // was SC(36): only left a 6px gap below the 30px-tall boxes, crowding the label below
  mk_label(TR("SF / CR / TX / AF"));
  {
    const int g = 6, fw = (cw - 3 * g) / 4;       // four equal fields filling the row
    _fields.sf_ta      = mk_ta(fw, 0, TR("SF"), 2);
    _fields.cr_ta      = mk_ta(fw, fw + g, TR("CR"), 2);
    _fields.tx_ta      = mk_ta(fw, 2 * (fw + g), TR("TX"), 4);
    _fields.airtime_ta = mk_ta(fw, 3 * (fw + g), TR("AF"), 6);
  }
  y += SC(38);

  if (prefs) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f", prefs->freq); lv_textarea_set_text(_fields.freq_ta, buf);
    snprintf(buf, sizeof(buf), "%.1f", prefs->bw); lv_textarea_set_text(_fields.bw_ta, buf);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(prefs->sf)); lv_textarea_set_text(_fields.sf_ta, buf);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(prefs->cr)); lv_textarea_set_text(_fields.cr_ta, buf);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(prefs->tx_power_dbm)); lv_textarea_set_text(_fields.tx_ta, buf);
    snprintf(buf, sizeof(buf), "%.2f", prefs->airtime_factor); lv_textarea_set_text(_fields.airtime_ta, buf);
  }

  if (_fields.radio_preset_dd) {
    const int match = findMatchingMeshRadioPreset(prefs);
    _presetSilent = true;
    lv_dropdown_set_selected(_fields.radio_preset_dd, match < 0 ? 0 : static_cast<uint16_t>(match + 1));
    _presetSilent = false;
  }

  // Airtime / duty-cycle readout — derived from the current PHY config so the user
  // can see EU-868 compliance without doing the math. MeshCore stores airtime_factor
  // = (100/duty) - 1, so duty% = 100/(af+1); paired with the time-on-air per packet.
  {
    const float af   = prefs ? prefs->airtime_factor : 0.0f;
    const int   duty = (int)(100.0f / (af + 1.0f) + 0.5f);
    const uint32_t toa = prefs ? loraToaMs(prefs->sf, prefs->bw, prefs->cr, 40) : 0;
    lv_obj_t* rl = lv_label_create(body);
    char rb[80];
    // No "≈" (U+2248) — not in the UI font, renders as tofu. "·" (U+00B7) is fine.
    snprintf(rb, sizeof rb, "~%d%% max duty cycle  \xC2\xB7  ~%lu ms / 40B packet",
             duty, (unsigned long)toa);
    lv_label_set_text(rl, rb);
    lv_obj_set_width(rl, _width - SC(4));
    lv_obj_set_style_text_font(rl, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(rl, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_pos(rl, 2, y);
    // Width is set, so this label WRAPS. A 4-digit time-on-air (any SF11/SF12
    // preset) or a longer translation takes it to two lines, and the fixed
    // SC(22) advance ran the second line through the MESH section divider.
    lv_obj_update_layout(rl);
    y += LV_MAX(SC(22), lv_obj_get_height(rl) + SC(4));
  }

  mk_section("MESH");

  // Answer telemetry requests — surfaces MeshCore's telemetry_mode_* gate (battery +
  // environment; location telemetry keeps its own separate setting so a simple
  // "allow" can't leak position). Off = ignore other nodes' requests for our telemetry.
  {
    // settingsRowLabel reserves the right 56 px for the switch; lv_obj_align
    // TOP_RIGHT then sits it flush to the body edge (the cw - SC(48) absolute pos
    // ran off-screen under the scrollbar).
    int h = settingsRowLabel(body, y, 4, TR("Answer telemetry requests"), colors().COLOR_TEXT, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (prefs && prefs->telemetry_mode_base != 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->telemetryAllowChangedCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(SC(40), h + SC(12));
  }

  // Drop 1-character messages. Off by default: this filters other people's traffic,
  // so it is a deliberate choice rather than something we decide for the operator.
  {
    int h = settingsRowLabel(body, y, 4, TR("Ignore 1-character messages"), colors().COLOR_TEXT, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetIgnoreTinyMsgs()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->ignoreTinyMsgsChangedCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(SC(40), h + SC(12));
  }
  {
    lv_obj_t* note = lv_label_create(body);
    lv_label_set_text(note, TR("Most mesh spam is a single character, because it is the cheapest "
                               "message to send. Dropped silently: no chat entry, no notification, no sound."));
    lv_obj_set_width(note, _width - SC(4));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(note, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(note, 2, y);
    lv_obj_update_layout(note);
    y += lv_obj_get_height(note) + SC(10);
  }

  // Position sharing (#266). Until now the only way to share position was to put it
  // in the advert, which broadcasts it in the clear to every node in range, forever.
  // MeshCore can answer position on REQUEST instead — encrypted to the contact that
  // asked, and only to contacts you picked — but nothing ever set telemetry_mode_loc,
  // so that whole path was unreachable. "Chosen contacts" reads the per-contact
  // permission set from each contact's own menu.
  mk_label(TR("Share my location when asked"));
  {
    lv_obj_t* dd = lv_dropdown_create(body);
    lv_obj_set_size(dd, lv_pct(100), SC(34));
    lv_obj_set_pos(dd, 2, y);
    // Order MUST match TELEM_MODE_*: 0 = deny, 1 = allow-flags, 2 = allow-all.
    lv_dropdown_set_options(dd, TR("Never\nChosen contacts only\nAnyone who asks"));
    lv_obj_set_style_text_font(dd, &font12(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_t* loclist = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(loclist, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(loclist, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(loclist, &font12(), LV_PART_MAIN);
    uint8_t lm = prefs ? prefs->telemetry_mode_loc : TELEM_MODE_DENY;
    if (lm > TELEM_MODE_ALLOW_ALL) lm = TELEM_MODE_DENY;
    lv_dropdown_set_selected(dd, lm);
    lv_obj_add_event_cb(dd, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->locTelemetryModeChangedCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(dd, _host.clampDropdown, LV_EVENT_CLICKED, nullptr);
    y += SC(40);
  }
  {
    // Be explicit about the two things that surprise people: this is answered on
    // request (nothing is broadcast), and MeshCore only replies at all when plain
    // telemetry is allowed, so the switch above is a prerequisite rather than an
    // unrelated setting.
    lv_obj_t* note = lv_label_create(body);
    lv_label_set_text(note, TR("Sent only when that contact asks, encrypted to them. Nothing is broadcast. "
                               "Needs \"Answer telemetry requests\" on. Pick contacts in a contact's menu."));
    lv_obj_set_width(note, _width - SC(4));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(note, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(note, 2, y);
    lv_obj_update_layout(note);
    y += lv_obj_get_height(note) + SC(10);
  }
#if defined(ESP32)
  // The advert displacement (#399) also applies to a position ANSWER, because a
  // privacy setting that a telemetry request walks straight around is a hole
  // rather than a feature. But an answer is not a broadcast: it is encrypted to
  // one contact you already granted the permission to, and it only goes out
  // because they asked. Letting those contacts have the true fix is a reasonable
  // thing to want (honza_87628), so make it a deliberate choice instead of an
  // assumption. Off by default, so the private behaviour is what you get unless
  // you say otherwise, and the broadcast advert stays displaced either way.
  {
    const int rh = settingsRowLabel(body, y, 6, TR("Exact position to those contacts"),
                                    colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    y += LV_MAX(34, rh + 12);
    if (touchPrefsGetTelemLocExact()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* e) {
      auto* self = static_cast<RadioSettingsScreen*>(lv_event_get_user_data(e));
      if (!self->accepts(e)) return;
      auto& _host = self->_host;
      if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
      const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
      touchPrefsSetTelemLocExact(on);
      if (_host.task()) {
        _host.task()->showAlert(on ? TR("Answering with your exact position")
                                : TR("Answering with your displaced position"), 1600);
      }
    }, LV_EVENT_VALUE_CHANGED, nullptr);
  }
#endif

  // Multi-byte routing: how many bytes of each repeater's hash this node stamps
  // into the path when it adverts / sends. 1 byte (legacy) collides in large
  // regions; 2-3 bytes disambiguate. Applied immediately + persisted (no radio
  // reconfig). All repeaters on a path must support it (MeshCore >= v1.14);
  // older ones silently drop 2/3-byte packets.
  mk_label(TR("Multi-byte routing (path hash)"));
  {
    lv_obj_t* dd = lv_dropdown_create(body);
    lv_obj_set_size(dd, lv_pct(100), SC(34));
    lv_obj_set_pos(dd, 2, y);
    lv_dropdown_set_options(dd, TR("1 byte (legacy)\n2 bytes\n3 bytes"));
    lv_obj_set_style_text_font(dd, &font12(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_t* phlist = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(phlist, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(phlist, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(phlist, &font12(), LV_PART_MAIN);
    uint8_t phm = prefs ? prefs->path_hash_mode : 0;
    if (phm > 2) phm = 0;
    lv_dropdown_set_selected(dd, phm);
    lv_obj_add_event_cb(dd, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->pathHashModeChangedCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(dd, _host.clampDropdown, LV_EVENT_CLICKED, nullptr);
    y += SC(40);
  }

  // Region scope: tags outgoing floods with a region so repeaters that only
  // re-flood their own region (region-scoped networks) still propagate them.
  // Public "#hashtag" region -> key = SHA256("#name"); blank = unscoped (default).
  mk_label(TR("Region scope (#tag, blank = none)"));
  _fields.region_ta = mk_ta(cw, 0, TR("#region"), TOUCH_REGION_SCOPE_MAXLEN - 1);
  {
    char rbuf[TOUCH_REGION_SCOPE_MAXLEN];
    if (touchPrefsGetRegionScope(rbuf, sizeof(rbuf)) > 0 && rbuf[0])
      lv_textarea_set_text(_fields.region_ta, rbuf);
  }
  y += SC(38);
  // Note: this region scope is the node-wide default. Each channel can override it
  // with its own region in that channel's settings.
  {
    lv_obj_t* note = lv_label_create(body);
    lv_label_set_text(note, TR("Channel-specific regions can be set in each channel's settings."));
    lv_obj_set_width(note, _width - SC(4));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(note, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(note, 2, y);
    lv_obj_update_layout(note);
    y += lv_obj_get_height(note) + SC(8);
  }
  // #271: the regions above are the ones we SEND under. This opens the list of
  // regions we can RECOGNISE. Naming them in message details needs only the
  // name, since the key is derived from it.
  {
    lv_obj_t* rb = lv_btn_create(body);
    lv_obj_set_size(rb, _width - SC(4), SC(34));
    lv_obj_set_pos(rb, 2, y);
    styleButton(rb);
    lv_obj_add_event_cb(rb, [](lv_event_t* e) {
      auto* self = static_cast<RadioSettingsScreen*>(lv_event_get_user_data(e));
      if (!self->accepts(e)) return;
      auto& _host = self->_host;
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      _host.closeModal();
      _host.openRegions();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* rl = lv_label_create(rb);
    lv_label_set_text(rl, TR(LV_SYMBOL_LIST "  Known regions"));
    lv_obj_set_style_text_font(rl, &font14(), LV_PART_MAIN);
    lv_obj_center(rl);
    y += SC(40);
  }

  // Opt-in: also scope DIRECT messages / logins / remote-management floods to the region
  // (not just channels). For networks where your only repeater re-floods a single region.
  {
    int rh = settingsRowLabel(body, y, 6, TR("Scope direct msgs to region"), colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetScopeDirect()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioScopeDirectToggleCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(34, rh + 12);

    lv_obj_t* note = lv_label_create(body);
    lv_label_set_text(note, TR("Enable only if your repeater re-floods just one region. Otherwise leave off — it can block cross-region login/DMs."));
    lv_obj_set_width(note, _width - SC(4));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(note, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(note, 2, y);
    lv_obj_update_layout(note);
    y += lv_obj_get_height(note) + SC(8);
  }

  mk_section("SIGNAL");

#if defined(HAS_TDISPLAY_P4)
  // T-Display P4 antenna select: the SKY13453 on XL9535 IO1. Confirmed against LilyGo's own driver
  // (HIGH = RF1 = on-board, LOW = RF2 = external socket) — see the note on Xl9535.h. Internal is the
  // default and is re-forced at every boot; external is session-only and gated behind a confirmation,
  // because keying the PA into an empty socket is how you destroy one. "Auto" keeps the old
  // per-transmit toggle purely for comparison: it sends internal and listens external, so Trace SNR
  // reads lopsided on it and roughly equal on a correctly pinned antenna.
  mk_label(TR("LoRa antenna (P4)"));
  {
    lv_obj_t* dd = lv_dropdown_create(body);
    lv_obj_set_size(dd, lv_pct(100), SC(34));
    lv_obj_set_pos(dd, 2, y);
    lv_dropdown_set_options(dd, TR("Internal (on-board)\nExternal (MMCX)\nAuto (legacy, per transmit)"));
    lv_obj_set_style_text_font(dd, &font12(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_t* antlist = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(antlist, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(antlist, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(antlist, &font12(), LV_PART_MAIN);
    lv_dropdown_set_selected(dd, xl9535.antennaMode());   // live state, not a stored pref
    lv_obj_add_event_cb(dd, _host.antennaSelect, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(dd, _host.clampDropdown, LV_EVENT_CLICKED, nullptr);
    y += SC(44);
    y += settingsRowLabel(body, y, 0,
                          TR("Always starts on the on-board antenna after a reboot. Only pick External "
                             "with an antenna fitted to the external socket."),
                          colors().COLOR_SUB, &font12(), 0) + 2;
  }
#endif

#if defined(HELTEC_LORA_V4_TFT)
  // Heltec V4.3 only: the external FEM's high-gain receive amplifier (~17 dB). Bypassed by
  // default on the plain V4 (ON by default on the V4-R8 since prefs v49); a big win in
  // quiet/remote sites, but can desensitize in noisy areas. This is
  // SEPARATE from the SX1262's tiny internal "boosted gain". Hidden on V4.2 (no switchable LNA).
  if (board.femLnaControllable()) {
    int rh = settingsRowLabel(body, y, 4, TR("High-gain receiver (FEM LNA)"), colors().COLOR_TEXT, &font12(), 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetFemLna()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, _host.femChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    y += LV_MAX(34, rh + 10);
    y += settingsRowLabel(body, y, 0, TR("~17 dB amp for quiet/remote areas; turn off in noisy spots."),
                          colors().COLOR_SUB, &font12(), 0) + 2;
  }
#endif

  // Buffered receive (experimental): decouple packet pickup from the UI loop.
  {
    int rh = settingsRowLabel(body, y, 4, TR("Buffered receive (experimental)"), colors().COLOR_TEXT, &font12(), 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetRxQueue()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioRxQueueToggleCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(34, rh + 10);
    y += settingsRowLabel(body, y, 0, TR("Reads packets on a background task so a busy screen can't drop them."),
                          colors().COLOR_SUB, &font12(), 0) + 2;
  }

  // Auto-retry until heard (#207): resend until the mesh echoes/ACKs the packet.
  {
    int rh = settingsRowLabel(body, y, 4, TR("Retry sends until heard"), colors().COLOR_TEXT, &font12(), 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetRetryEcho()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioRetryEchoToggleCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(34, rh + 10);
    y += settingsRowLabel(body, y, 0, TR("Resends a message until a repeater echoes it or the recipient ACKs. Turn off to send once only."),
                          colors().COLOR_SUB, &font12(), 0) + 2;
  }

  // Signal probe — same setting as the home-graph Signal popup. A periodic
  // discover advert keeps the signal bars / graph fresh. Poll interval in whole
  // minutes (1..1440); the toggle saves immediately, the field on its Set button.
  mk_label(TR("Signal probe"));
  {
    int rh = settingsRowLabel(body, y, 6, TR("Auto-discover"), colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetSigProbeEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioSigProbeToggleCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(34, rh + 12);

    lv_obj_t* pl = lv_label_create(body);
    lv_label_set_text(pl, TR("Poll (min)"));
    lv_obj_set_style_text_font(pl, &font14(), LV_PART_MAIN);   // accents need the fallback chain
    lv_obj_set_style_text_color(pl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(pl, 2, y + 6);
    _signalPoll = lv_textarea_create(body);
    lv_obj_set_size(_signalPoll, SC(50), SC(30));
    lv_obj_set_pos(_signalPoll, cw - 114, y);
    lv_textarea_set_one_line(_signalPoll, true);
    lv_textarea_set_max_length(_signalPoll, 4);
    _host.attachField(_signalPoll);
    lv_obj_add_event_cb(_signalPoll, +[](lv_event_t* event){ static_cast<RadioSettingsScreen*>(lv_event_get_user_data(event))->radioSigPollSaveCb(event); }, LV_EVENT_DEFOCUSED, this);  // auto-save on blur
    { char pb[8]; snprintf(pb, sizeof pb, "%u", (unsigned)touchPrefsGetSigPollMins());
      lv_textarea_set_text(_signalPoll, pb); }
    y += SC(38);
  }

  // Auto advert — a shortcut to the Advertise app, where you send an advert now and set the
  // periodic flood / local self-advert timers. Surfaced here so it's discoverable from Radio & Mesh.
  mk_label(TR("Auto advert"));
  {
    lv_obj_t* b_adv = lv_btn_create(body);
    lv_obj_set_size(b_adv, lv_pct(100), SC(34));
    lv_obj_set_pos(b_adv, 2, y);
    styleButton(b_adv);
    lv_obj_add_event_cb(b_adv, _host.openAdvert, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l_adv = lv_label_create(b_adv);
    useChainedFont(l_adv);
    lv_label_set_text_fmt(l_adv, LV_SYMBOL_UPLOAD "  %s", TR("Open Advertise app"));
    lv_obj_center(l_adv);
    y += SC(42);
    y += settingsRowLabel(body, y, 0, TR("Send an advert now, or set how often you re-advertise."),
                          colors().COLOR_SUB, &font12(), 0) + 2;
  }

  // No "Apply radio params" button — each field auto-applies on blur (saveRadioParamsCb
  // via LV_EVENT_DEFOCUSED, silent until the whole group is valid).
}

void RadioSettingsScreen::saveRadioParamsCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (_host.deleting(e)) return;   // the widget is being destroyed
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_DEFOCUSED) || !_host.task()) return;
  const bool silent = (_c == LV_EVENT_DEFOCUSED);   // blur auto-save: apply quietly, no alerts
  _host.syncKeyboard();
  float freq = 0.0f, bw = 0.0f, af = 0.0f;
  int sf = 0, cr = 0, tx = 0;
  if (!parseFloatField(_fields.freq_ta, freq) || !parseFloatField(_fields.bw_ta, bw) ||
      !parseIntField(_fields.sf_ta, sf) || !parseIntField(_fields.cr_ta, cr) ||
      !parseIntField(_fields.tx_ta, tx) || !parseFloatField(_fields.airtime_ta, af)) {
    if (!silent) _host.task()->showAlert(TR("Invalid radio values"), 1200);
    return;
  }
  if (tx > (int)MAX_LORA_TX_POWER) {   // radio can't exceed this — clamp + notify (else it appears to "reset" to the max on reboot)
    tx = (int)MAX_LORA_TX_POWER;
    char m[40]; snprintf(m, sizeof m, "TX max is %d dBm", tx);
    if (!silent) _host.task()->showAlert(m, 1500);
    if (_fields.tx_ta) { char v[8]; snprintf(v, sizeof v, "%d", tx); lv_textarea_set_text(_fields.tx_ta, v); }   // reflect the clamp in the field
  }
  // Region scope — trimmed the same way it's persisted, so it can be compared below.
  char region[TOUCH_REGION_SCOPE_MAXLEN] = {0};
  const bool has_region_ta = (_fields.region_ta != nullptr);
  if (has_region_ta) {
    strncpy(region, lv_textarea_get_text(_fields.region_ta), sizeof(region) - 1);
    char* r = region;                                    // trim so the stored name matches the key
    while (*r == ' ' || *r == '\t') r++;
    size_t rl = strlen(r);
    while (rl && (r[rl-1]==' '||r[rl-1]=='\t'||r[rl-1]=='\n'||r[rl-1]=='\r')) r[--rl] = '\0';
    memmove(region, r, strlen(r) + 1);
  }
#if defined(TLORA_PAGER)
  // Blur auto-save fires on EVERY field defocus, including pure keyboard/encoder nav that
  // never edits anything (no touch fallback on the pager — moving focus off a field IS a
  // defocus). Skip the flash write + live radio SPI reconfigure when nothing actually
  // changed, so tabbing through this row doesn't retrigger a several-hundred-ms-to-
  // multi-second radio reinit per field — observed on hardware as escalating
  // [STALL] ui:lvgl entries while navigating this screen with no edits made. Touch boards
  // don't hit this (a field only blurs on a deliberate tap-out after an edit), so they
  // keep the original always-save-on-blur behavior.
  if (silent) {
    NodePrefs* prefs = the_mesh.getNodePrefs();
    char cur_region[TOUCH_REGION_SCOPE_MAXLEN] = {0};
    if (has_region_ta) touchPrefsGetRegionScope(cur_region, sizeof(cur_region));
    const bool unchanged = prefs
        && std::fabs(prefs->freq - freq) <= 0.002
        && std::fabs(prefs->bw - bw) <= 0.02
        && prefs->sf == (uint8_t)sf
        && prefs->cr == (uint8_t)cr
        && prefs->tx_power_dbm == (int8_t)tx
        && std::fabs(static_cast<double>(prefs->airtime_factor) - af) <= 0.005
        && (!has_region_ta || strcmp(cur_region, region) == 0);
    if (unchanged) return;
  }
#endif
  bool ok = _host.task()->setRadioParams(freq, bw, static_cast<uint8_t>(sf), static_cast<uint8_t>(cr),
                                      static_cast<int8_t>(tx), af);
  // Region scope — independent of the freq/SF values above. Derive + persist the
  // flood-scope key from the typed "#region" (blank clears it back to unscoped),
  // and remember the display name for next time the form is shown.
  bool has_region = false;
  if (has_region_ta) {
    the_mesh.setDefaultFloodScope(region);
    touchPrefsSetRegionScope(region);
    // #271: keep the registry in step with the region we just adopted, so its
    // messages are named immediately. Any previously-registered region keeps its
    // slot, so changing our own region does not un-name the history we
    // received while we were in the old one.
    if (region[0]) the_mesh.regionRegistry().ensureRegion(region);
    has_region = (region[0] != '\0');
  }
  if (ok) {
    if (!silent) _host.task()->showAlert(has_region ? TR("Radio + region set") : TR("Radio applied"), 1000);
    _host.refresh();
  }
}

void RadioSettingsScreen::applyMeshRadioPresetFields(unsigned preset_idx) {
  if (preset_idx >= k_mesh_radio_preset_count || !_fields.freq_ta) return;
  _host.syncKeyboard();
  const MeshRadioPreset& p = k_mesh_radio_presets[preset_idx];
  char buf[28];
  snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(p.freq_mhz));
  lv_textarea_set_text(_fields.freq_ta, buf);
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(p.bw_khz));
  lv_textarea_set_text(_fields.bw_ta, buf);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(p.sf));
  lv_textarea_set_text(_fields.sf_ta, buf);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(p.cr));
  lv_textarea_set_text(_fields.cr_ta, buf);
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(p.tx_dbm));
  lv_textarea_set_text(_fields.tx_ta, buf);
  // #161: MeshCore's airtime_factor is a TX pacing multiplier -- the core's Dispatcher runs at
  // duty = 1/(1+af). A 10% duty limit therefore needs af = 9, NOT 0.10: the old pct/100 write
  // configured every preset user's radio to ~91% duty (and the Home meter honestly said so).
  const float af = meshPresetAirtimeFactor(p.airtime_limit_pct);
  snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(af));
  lv_textarea_set_text(_fields.airtime_ta, buf);
}

void RadioSettingsScreen::radioPresetChangedCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || _presetSilent) return;
  if (!_fields.radio_preset_dd || !_fields.freq_ta) return;
  const uint16_t sel = lv_dropdown_get_selected(_fields.radio_preset_dd);
  if (sel == 0) return;
  const unsigned idx = static_cast<unsigned>(sel - 1u);
  if (idx >= k_mesh_radio_preset_count) return;
  applyMeshRadioPresetFields(idx);
}

void RadioSettingsScreen::ignoreTinyMsgsChangedCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !_host.task()) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touchPrefsSetIgnoreTinyMsgs(on);
  _host.task()->showAlert(on ? TR("1-character messages will be ignored")
                          : TR("1-character messages will be shown"), 1600);
}

void RadioSettingsScreen::locTelemetryModeChangedCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !_host.task()) return;
  const uint8_t sel = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
  _host.task()->setLocationTelemetryMode(sel);
  const char* msg = (sel == TELEM_MODE_ALLOW_ALL)   ? TR("Location shared with anyone who asks")
                  : (sel == TELEM_MODE_ALLOW_FLAGS) ? TR("Location shared with chosen contacts only")
                                                    : TR("Location sharing off");
  _host.task()->showAlert(msg, 1600);
}

void RadioSettingsScreen::pathHashModeChangedCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !_host.task()) return;
  uint8_t sel = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
  _host.task()->setPathHashMode(sel);   // existing setter: clamps to 0..2 + savePrefs
  char m[40];
  snprintf(m, sizeof m, TR("Path hash: %u byte%s"), (unsigned)(sel + 1), sel ? "s" : "");
  _host.task()->showAlert(m, 1400);
}

void RadioSettingsScreen::radioSigProbeToggleCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  touchPrefsSetSigProbeEnabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

void RadioSettingsScreen::radioScopeDirectToggleCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touchPrefsSetScopeDirect(on);
  the_mesh.setScopeDirectFloods(on);
}

void RadioSettingsScreen::radioRxQueueToggleCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touchPrefsSetRxQueue(on);
  radio_driver.rxQueueEnable(on);
}

void RadioSettingsScreen::radioRetryEchoToggleCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touchPrefsSetRetryEcho(on);
  the_mesh.setCompanionRetryEnabled(on);
}

void RadioSettingsScreen::radioSigPollSaveCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (_host.deleting(e)) return;   // the widget is being destroyed
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_DEFOCUSED) || !_signalPoll) return;
  _host.syncKeyboard();
  int m = atoi(lv_textarea_get_text(_signalPoll));
  if (m < 1)    m = 1;
  if (m > 1440) m = 1440;
  touchPrefsSetSigPollMins((uint16_t)m);
  char b[8]; snprintf(b, sizeof b, "%d", m);
  lv_textarea_set_text(_signalPoll, b);
  if (_host.task()) { char msg[40]; snprintf(msg, sizeof msg, TR("Poll every %d min"), m); _host.task()->showAlert(msg, 1100); }
}

void RadioSettingsScreen::telemetryAllowChangedCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !_host.task()) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  _host.task()->setTelemetryAllow(on);
  _host.task()->showAlert(on ? TR("Telemetry requests allowed") : TR("Telemetry requests off"), 1400);
}
} }
