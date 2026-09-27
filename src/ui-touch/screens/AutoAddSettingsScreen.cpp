#include "AutoAddSettingsScreen.h"
#include "../platform/UiDevice.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/FormFields.h"
using namespace ui::widgets;
namespace ui { namespace screens {
using namespace theme;
constexpr uint8_t AUTO_ADD_OVERWRITE_OLDEST = 1, AUTO_ADD_CHAT = 2, AUTO_ADD_REPEATER = 4, AUTO_ADD_ROOM_SERVER = 8, AUTO_ADD_SENSOR = 16;
void AutoAddSettingsScreen::build(lv_obj_t* body, lv_coord_t width) {
  adopt(body, width);
  if (!body) return;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
  auto mk_switch = [&](const char* text, lv_obj_t** out) {
    int h = settingsRowLabel(body, y, 6, text, colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<AutoAddSettingsScreen*>(lv_event_get_user_data(event))->autoAddSwitchCb(event); }, LV_EVENT_VALUE_CHANGED, this);  // auto-save on toggle
    if (out) *out = sw;
    y += LV_MAX(34, h + 12);
  };
  mk_switch(TR("Auto chat"), &_fields.auto_chat_sw);
  mk_switch(TR("Auto repeater"), &_fields.auto_rep_sw);
  mk_switch(TR("Auto room"), &_fields.auto_room_sw);
  mk_switch(TR("Auto sensor"), &_fields.auto_sensor_sw);
  // #178: favourites are already exempt — the core's eviction loop skips any
  // contact with the favourite flag — but the label never said so, leaving users
  // to guess whether starring a contact protected it while roaming.
  mk_switch(TR("Overwrite oldest non-favorite"), &_fields.auto_overwrite_sw);
  _fields.manual_add_sw = nullptr;   // master "manual add" removed — per-type switches are authoritative now

  // "Notify on new contact" — a UI pref (not a NodePrefs autoadd bit), so it gets
  // its own callback + persistence rather than mk_switch's autoAddSwitchCb.
  {
    int h = settingsRowLabel(body, y, 6, TR("Notify on new contact"), colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
    if (touchPrefsGetNewContactToast()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, +[](lv_event_t* event){ static_cast<AutoAddSettingsScreen*>(lv_event_get_user_data(event))->toggleNewContactToastCb(event); }, LV_EVENT_VALUE_CHANGED, this);
    y += LV_MAX(34, h + 12);
  }

  lv_obj_t* hops_l = lv_label_create(body);
  useChainedFont(hops_l);
  lv_label_set_text(hops_l, TR("Max hops (0..64)"));
  lv_obj_set_style_text_color(hops_l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(hops_l, 2, y + 6);
  _fields.max_hops_ta = lv_textarea_create(body);
  lv_obj_set_size(_fields.max_hops_ta, SC(80), SC(30));
  lv_obj_set_pos(_fields.max_hops_ta, 142, y);
  lv_textarea_set_one_line(_fields.max_hops_ta, true);
  lv_textarea_set_max_length(_fields.max_hops_ta, 3);
  _host.attachField(_fields.max_hops_ta);
  lv_obj_add_event_cb(_fields.max_hops_ta, +[](lv_event_t* event){ static_cast<AutoAddSettingsScreen*>(lv_event_get_user_data(event))->saveAutoAddCb(event); }, LV_EVENT_DEFOCUSED, this);  // auto-save on blur (switches already save on toggle)
  y += SC(38);

  if (prefs) {
    // Old "add everything" mode (manual_add_contacts == 0) ignored the per-type
    // bits, so reflect it by showing every Auto switch ON — they're authoritative
    // now, so the user can genuinely turn a type off.
    const bool add_all = (prefs->manual_add_contacts & 1) == 0;
    if (add_all || (prefs->autoadd_config & AUTO_ADD_CHAT)) lv_obj_add_state(_fields.auto_chat_sw, LV_STATE_CHECKED);
    if (add_all || (prefs->autoadd_config & AUTO_ADD_REPEATER)) lv_obj_add_state(_fields.auto_rep_sw, LV_STATE_CHECKED);
    if (add_all || (prefs->autoadd_config & AUTO_ADD_ROOM_SERVER)) lv_obj_add_state(_fields.auto_room_sw, LV_STATE_CHECKED);
    if (add_all || (prefs->autoadd_config & AUTO_ADD_SENSOR)) lv_obj_add_state(_fields.auto_sensor_sw, LV_STATE_CHECKED);
    if (prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) lv_obj_add_state(_fields.auto_overwrite_sw, LV_STATE_CHECKED);
    char hops_buf[8];
    snprintf(hops_buf, sizeof(hops_buf), "%u", static_cast<unsigned>(prefs->autoadd_max_hops));
    lv_textarea_set_text(_fields.max_hops_ta, hops_buf);
  }

  // No "Save auto-add" button — switches save on toggle, max-hops auto-saves on blur.
}

void AutoAddSettingsScreen::saveAutoAddCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (_host.deleting(e)) return;   // the widget is being destroyed
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_DEFOCUSED) || !_host.task()) return;
  const bool silent = (_c == LV_EVENT_DEFOCUSED);   // blur auto-save: quiet if mid-edit
  _host.syncKeyboard();
  int max_hops = 0;
  if (!parseIntField(_fields.max_hops_ta, max_hops)) {
    if (!silent) _host.task()->showAlert(TR("Invalid max hops"), 1200);
    return;
  }
  if (max_hops < 0) max_hops = 0;
  if (max_hops > 64) max_hops = 64;

  uint8_t mask = 0;
  if (_fields.auto_overwrite_sw && lv_obj_has_state(_fields.auto_overwrite_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_OVERWRITE_OLDEST;
  if (_fields.auto_chat_sw && lv_obj_has_state(_fields.auto_chat_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_CHAT;
  if (_fields.auto_rep_sw && lv_obj_has_state(_fields.auto_rep_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_REPEATER;
  if (_fields.auto_room_sw && lv_obj_has_state(_fields.auto_room_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_ROOM_SERVER;
  if (_fields.auto_sensor_sw && lv_obj_has_state(_fields.auto_sensor_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_SENSOR;
  uint8_t manual = 1u;   // touch UI is always selective: the per-type Auto switches are authoritative (off = really off)

  _host.task()->setAutoAddConfig(mask, static_cast<uint8_t>(max_hops), manual);
  if (!silent) _host.task()->showAlert(TR("Auto-add saved"), 1000);
  _host.refresh();
}

void AutoAddSettingsScreen::autoAddSwitchCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !_host.task()) return;
  uint8_t mask = 0;
  if (_fields.auto_overwrite_sw && lv_obj_has_state(_fields.auto_overwrite_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_OVERWRITE_OLDEST;
  if (_fields.auto_chat_sw && lv_obj_has_state(_fields.auto_chat_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_CHAT;
  if (_fields.auto_rep_sw && lv_obj_has_state(_fields.auto_rep_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_REPEATER;
  if (_fields.auto_room_sw && lv_obj_has_state(_fields.auto_room_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_ROOM_SERVER;
  if (_fields.auto_sensor_sw && lv_obj_has_state(_fields.auto_sensor_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_SENSOR;
  uint8_t manual = 1u;   // touch UI is always selective: the per-type Auto switches are authoritative (off = really off)
  int max_hops = 3;
  int mh;
  if (_fields.max_hops_ta && parseIntField(_fields.max_hops_ta, mh)) max_hops = mh;
  if (max_hops < 0) max_hops = 0;
  if (max_hops > 64) max_hops = 64;
  _host.task()->setAutoAddConfig(mask, static_cast<uint8_t>(max_hops), manual);
}

void AutoAddSettingsScreen::toggleNewContactToastCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  touchPrefsSetNewContactToast(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}
} }
