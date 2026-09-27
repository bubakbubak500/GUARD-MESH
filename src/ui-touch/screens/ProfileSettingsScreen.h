// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SettingsForm.h"
namespace ui { namespace screens {
class ProfileSettingsScreen : public SettingsForm {
public:
  using SettingsForm::SettingsForm;
  void build(lv_obj_t*, lv_coord_t);
private:
  struct Fields {
    lv_obj_t* lat_ta = nullptr;
    lv_obj_t* lon_ta = nullptr;
    lv_obj_t* name_ta = nullptr;
    lv_obj_t* share_loc_sw = nullptr;
  } _fields;
  void saveProfileNameCb(lv_event_t*);
  void saveProfilePosCb(lv_event_t*);
  void savePolicyCb(lv_event_t*);
};
} }
