// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SettingsForm.h"
namespace ui { namespace screens {
class MqttSettingsScreen : public SettingsForm {
public:
  using SettingsForm::SettingsForm;
  void build(lv_obj_t*, lv_coord_t);
private:
  struct Fields {
    lv_obj_t* mqtt_ch_sw = nullptr;
    lv_obj_t* mqtt_consent_cb = nullptr;
    lv_obj_t* mqtt_dm_sw = nullptr;
    lv_obj_t* mqtt_en_sw = nullptr;
    lv_obj_t* mqtt_host_ta = nullptr;
    lv_obj_t* mqtt_port_ta = nullptr;
    lv_obj_t* mqtt_psk_ta = nullptr;
    lv_obj_t* mqtt_pwd_ta = nullptr;
    lv_obj_t* mqtt_user_ta = nullptr;
  } _fields;
  void mqttConsentCb(lv_event_t*);
  void mqttSaveCb(lv_event_t*);
};
} }
