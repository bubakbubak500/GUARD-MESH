// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SettingsForm.h"
namespace ui { namespace screens {
class RadioSettingsScreen : public SettingsForm {
public:
  using SettingsForm::SettingsForm;
  void build(lv_obj_t*, lv_coord_t);
private:
  struct Fields {
    lv_obj_t* airtime_ta = nullptr;
    lv_obj_t* bw_ta = nullptr;
    lv_obj_t* cr_ta = nullptr;
    lv_obj_t* freq_ta = nullptr;
    lv_obj_t* radio_preset_dd = nullptr;
    lv_obj_t* region_ta = nullptr;
    lv_obj_t* sf_ta = nullptr;
    lv_obj_t* tx_ta = nullptr;
  } _fields;
  bool _presetSilent = false;
  lv_obj_t* _signalPoll = nullptr;
  void saveRadioParamsCb(lv_event_t*);
  void applyMeshRadioPresetFields (unsigned);
  void radioPresetChangedCb(lv_event_t*);
  void ignoreTinyMsgsChangedCb(lv_event_t*);
  void locTelemetryModeChangedCb(lv_event_t*);
  void pathHashModeChangedCb(lv_event_t*);
  void radioSigProbeToggleCb(lv_event_t*);
  void radioScopeDirectToggleCb(lv_event_t*);
  void radioRxQueueToggleCb(lv_event_t*);
  void radioRetryEchoToggleCb(lv_event_t*);
  void radioSigPollSaveCb(lv_event_t*);
  void telemetryAllowChangedCb(lv_event_t*);
};
} }
