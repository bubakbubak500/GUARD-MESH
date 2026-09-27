// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SettingsForm.h"
namespace ui { namespace screens {
class AutoAddSettingsScreen : public SettingsForm {
public:
  using SettingsForm::SettingsForm;
  void build(lv_obj_t*, lv_coord_t);
private:
  struct Fields {
    lv_obj_t* auto_chat_sw = nullptr;
    lv_obj_t* auto_overwrite_sw = nullptr;
    lv_obj_t* auto_rep_sw = nullptr;
    lv_obj_t* auto_room_sw = nullptr;
    lv_obj_t* auto_sensor_sw = nullptr;
    lv_obj_t* manual_add_sw = nullptr;
    lv_obj_t* max_hops_ta = nullptr;
  } _fields;
  void saveAutoAddCb(lv_event_t*);
  void autoAddSwitchCb(lv_event_t*);
  void toggleNewContactToastCb(lv_event_t*);
};
} }
