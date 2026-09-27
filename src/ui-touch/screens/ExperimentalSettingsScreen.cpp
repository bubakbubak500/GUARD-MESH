#include "ExperimentalSettingsScreen.h"
#include "../platform/UiDevice.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/FormFields.h"
using namespace ui::widgets;
namespace ui { namespace screens {
using namespace theme;
void ExperimentalSettingsScreen::build(lv_obj_t* body, lv_coord_t width) {
  adopt(body, width);
  if (!body) return;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
  auto mk_switch = [&](const char* text, lv_obj_t** out) {
    int h = settingsRowLabel(body, y, 6, text, colors().COLOR_SUB, nullptr, 56);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
    if (out) *out = sw;
    y += LV_MAX(34, h + 12);
  };

  mk_switch(TR("Multi ACKs"), &_fields.exp_multi_sw);
  mk_switch(TR("Client repeat"), &_fields.exp_repeat_sw);
  mk_switch(TR("RX boosted gain"), &_fields.exp_boost_sw);
  mk_switch(TR("Duty meter"), &_fields.exp_dc_sw);

  if (prefs) {
    if (prefs->multi_acks) lv_obj_add_state(_fields.exp_multi_sw, LV_STATE_CHECKED);
    if (prefs->client_repeat) lv_obj_add_state(_fields.exp_repeat_sw, LV_STATE_CHECKED);
    if (prefs->rx_boosted_gain) lv_obj_add_state(_fields.exp_boost_sw, LV_STATE_CHECKED);
  }
#if defined(ESP32)
  if (touchPrefsGetDutyMeterShown()) lv_obj_add_state(_fields.exp_dc_sw, LV_STATE_CHECKED);
#endif

  // No "Save experimental" button — each switch applies instantly on toggle (the cb
  // re-reads all four states). Wired after the initial add_state so it isn't triggered.
  lv_obj_add_event_cb(_fields.exp_multi_sw, +[](lv_event_t* event){ static_cast<ExperimentalSettingsScreen*>(lv_event_get_user_data(event))->saveExperimentalCb(event); }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(_fields.exp_repeat_sw, +[](lv_event_t* event){ static_cast<ExperimentalSettingsScreen*>(lv_event_get_user_data(event))->saveExperimentalCb(event); }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(_fields.exp_boost_sw, +[](lv_event_t* event){ static_cast<ExperimentalSettingsScreen*>(lv_event_get_user_data(event))->saveExperimentalCb(event); }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(_fields.exp_dc_sw, +[](lv_event_t* event){ static_cast<ExperimentalSettingsScreen*>(lv_event_get_user_data(event))->saveExperimentalCb(event); }, LV_EVENT_VALUE_CHANGED, this);
}

void ExperimentalSettingsScreen::saveExperimentalCb(lv_event_t* e) {
  if (!accepts(e)) return;
  const lv_event_code_t _c = lv_event_get_code(e);
  if ((_c != LV_EVENT_CLICKED && _c != LV_EVENT_VALUE_CHANGED) || !_host.task()) return;
  uint8_t multi = (_fields.exp_multi_sw && lv_obj_has_state(_fields.exp_multi_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  uint8_t repeat = (_fields.exp_repeat_sw && lv_obj_has_state(_fields.exp_repeat_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  uint8_t boost = (_fields.exp_boost_sw && lv_obj_has_state(_fields.exp_boost_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  _host.task()->setExperimentalFlags(multi, repeat, boost);
#if defined(ESP32)
  bool dc_show = (_fields.exp_dc_sw && lv_obj_has_state(_fields.exp_dc_sw, LV_STATE_CHECKED));
  touchPrefsSetDutyMeterShown(dc_show);
#endif
  _host.refresh();   // applies instantly on toggle (no Save button)
}
} }
