// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#if defined(GUARD_SIMULATOR)
#include "SimTypes.h"
#else
#include "../../NodePrefs.h"
#endif
namespace ui { namespace radio {
struct MeshRadioPreset {
  const char* label;
  float       freq_mhz;
  float       bw_khz;
  uint8_t     sf;
  uint8_t     cr;
  int8_t       tx_dbm;
  uint8_t      airtime_limit_pct;  // web % → NodePrefs.airtime_factor = pct / 100
};
constexpr size_t k_mesh_radio_preset_count = 21;
extern const MeshRadioPreset k_mesh_radio_presets[k_mesh_radio_preset_count];
float meshPresetAirtimeFactor(uint8_t);
int findMatchingMeshRadioPreset(const NodePrefs*);
uint32_t loraToaMs(uint8_t, float, uint8_t, int);
} }
