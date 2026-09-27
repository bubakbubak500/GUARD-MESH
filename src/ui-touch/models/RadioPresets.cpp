#include "RadioPresets.h"
#include <cmath>
namespace ui { namespace radio {
const MeshRadioPreset k_mesh_radio_presets[k_mesh_radio_preset_count] = {
    {"Australia", 915.8f, 250.f, 10, 5, 22, 10},
    {"Australia (Mid)", 915.075f, 125.f, 9, 5, 22, 10},   // NSW/Canberra migration target (issue #121)
    {"Australia (Narrow)", 916.575f, 62.5f, 7, 8, 22, 10},
    {"Australia: SA, WA", 923.125f, 62.5f, 8, 8, 22, 10},
    {"Australia: QLD", 923.125f, 62.5f, 8, 5, 22, 10},
    {"Australia: NSW (SF11)", 915.8f, 250.f, 11, 5, 22, 10},
    {"New Zealand", 917.375f, 250.f, 11, 5, 22, 10},
    {"New Zealand (Narrow)", 917.375f, 62.5f, 7, 5, 22, 10},
    {"EU/UK (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"EU/UK (Deprecated)", 869.525f, 250.f, 11, 5, 22, 10},
    {"Switzerland (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"Czech Republic (Narrow)", 869.432f, 62.5f, 7, 5, 22, 10},
    {"Slovakia (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"Portugal (Narrow)", 869.618f, 62.5f, 7, 6, 22, 10},   // community PT preset (issue #74)
    {"EU 433MHz (Long Range)", 433.65f, 250.f, 11, 5, 22, 10},
    {"US/Canada", 910.525f, 62.5f, 7, 5, 22, 10},
    {"USA: SoCal (Community)", 927.875f, 62.5f, 7, 5, 22, 10},
    {"USA: San Francisco (Community)", 910.525f, 62.5f, 7, 5, 22, 10},
    {"USA: Sacramento (Community)", 909.875f, 62.5f, 9, 5, 22, 10},
    {"USA: Pacific NW (Community)", 910.525f, 62.5f, 7, 5, 22, 10},
    {"Brazil", 923.125f, 62.5f, 8, 8, 22, 10},   // official MeshCore BR preset — same params as Australia: SA, WA (issue #69)
};


float meshPresetAirtimeFactor(uint8_t pct) {
  if (pct == 0 || pct >= 100) return 0.0f;                 // unlimited
  float af = (100.0f / (float)pct) - 1.0f;
  return af > 9.0f ? 9.0f : af;
}
int findMatchingMeshRadioPreset(const NodePrefs* prefs) {
  if (!prefs) return -1;
  for (size_t i = 0; i < k_mesh_radio_preset_count; ++i) {
    const MeshRadioPreset& p = k_mesh_radio_presets[i];
    if (std::fabs(prefs->freq - p.freq_mhz) > 0.002) continue;
    if (std::fabs(prefs->bw - p.bw_khz) > 0.02) continue;
    if (prefs->sf != p.sf) continue;
    if (prefs->cr != p.cr) continue;
    if (prefs->tx_power_dbm != p.tx_dbm) continue;
    const double exp_af = (double)meshPresetAirtimeFactor(p.airtime_limit_pct);
    const double old_af = static_cast<double>(p.airtime_limit_pct) / 100.0;   // pre-#161 buggy write
    const double cur    = static_cast<double>(prefs->airtime_factor);
    if (std::fabs(cur - exp_af) > 0.05 && std::fabs(cur - old_af) > 0.05) continue;
    return static_cast<int>(i);
  }
  return -1;
}
uint32_t loraToaMs(uint8_t sf, float bw_khz, uint8_t crDen, int payload) {
  if (sf < 7) sf = 7;
  if (bw_khz < 1.0f) bw_khz = 250.0f;
  if (crDen < 5 || crDen > 8) crDen = 5;       // MeshCore cr is 5..8 == coding rate 4/5..4/8
  const double bw   = (double)bw_khz * 1000.0;
  const double tSym = (double)(1u << sf) / bw;
  const int    de   = (tSym > 0.016) ? 1 : 0;  // low-data-rate optimize (SF11/12 @ 125 kHz)
  const double tPreamble = (8 + 4.25) * tSym;  // 8-symbol preamble
  const int    num  = 8 * payload - 4 * (int)sf + 28 + 16;   // +16 = CRC on, explicit header
  const int    den  = 4 * ((int)sf - 2 * de);
  double pe = ceil((double)num / (double)(den > 0 ? den : 1)) * (double)crDen;
  if (pe < 0) pe = 0;
  return (uint32_t)((tPreamble + (8 + pe) * tSym) * 1000.0);
}
} }
