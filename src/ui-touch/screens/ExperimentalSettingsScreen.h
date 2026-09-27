// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SettingsForm.h"
namespace ui { namespace screens {
class ExperimentalSettingsScreen : public SettingsForm {
public:
  using SettingsForm::SettingsForm;
  void build(lv_obj_t*, lv_coord_t);
private:
  struct Fields {
    lv_obj_t* exp_boost_sw = nullptr;
    lv_obj_t* exp_dc_sw = nullptr;
    lv_obj_t* exp_multi_sw = nullptr;
    lv_obj_t* exp_repeat_sw = nullptr;
  } _fields;
  void saveExperimentalCb(lv_event_t*);
};
} }
