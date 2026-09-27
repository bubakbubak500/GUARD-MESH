#include "ConfigurationService.h"
#include "../platform/UiPlatform.h"
#include <algorithm>
#include <cstring>
namespace ui {
bool ConfigurationService::setNodeName(const char* s) {
  if (!_prefs) return false;
  if (!s) s = "";
  strncpy(_prefs->node_name, s, sizeof(_prefs->node_name) - 1);
  _prefs->node_name[sizeof(_prefs->node_name) - 1] = '\0';
  // Report the real write result — the "Name saved" toast used to show even
  // when the storage write silently failed.
  return platform::savePreferences();
}

bool ConfigurationService::setRadioParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_dbm, float airtime_factor) {
  if (!_prefs) return false;
  // Clamp TX to the radio's range here — the same bound the boot-time load applies — so a too-high
  // value is corrected the moment it's set + saved, not silently dropped on the next reboot.
  tx_dbm = static_cast<int8_t>(std::max(-9, std::min(int(tx_dbm), platform::maxTransmitPower())));
  _prefs->freq = freq_mhz;
  _prefs->bw = bw_khz;
  _prefs->sf = sf;
  _prefs->cr = cr;
  _prefs->tx_power_dbm = tx_dbm;
  _prefs->airtime_factor = airtime_factor;
  platform::savePreferences();
  platform::applyRadioPreferences();   // take effect immediately — no reboot needed
  return true;
}

void ConfigurationService::setAutoAddConfig(uint8_t mask, uint8_t max_hops, uint8_t manual_add) {
  if (!_prefs) return;
  _prefs->autoadd_config = mask;
  _prefs->autoadd_max_hops = max_hops;
  _prefs->manual_add_contacts = manual_add ? 1u : 0u;
  platform::savePreferences();
}

void ConfigurationService::setAdvertLocationPolicy(uint8_t policy) {
  if (!_prefs) return;
  _prefs->advert_loc_policy = policy;
  platform::savePreferences();
}

void ConfigurationService::setPathHashMode(uint8_t mode) {
  if (!_prefs) return;
  if (mode > 2) mode = 2;
  _prefs->path_hash_mode = mode;
  platform::savePreferences();
}

void ConfigurationService::setTelemetryAllow(bool on) {
  if (!_prefs) return;
  // Battery + environment telemetry only — location telemetry keeps its own separate
  // setting so this simple toggle can't expose position.
  _prefs->telemetry_mode_base = on ? TELEM_MODE_ALLOW_ALL : TELEM_MODE_DENY;
  _prefs->telemetry_mode_env  = on ? TELEM_MODE_ALLOW_ALL : TELEM_MODE_DENY;
  platform::savePreferences();
}

void ConfigurationService::setLocationTelemetryMode(uint8_t mode) {
  if (!_prefs) return;
  if (mode > TELEM_MODE_ALLOW_ALL) mode = TELEM_MODE_DENY;
  _prefs->telemetry_mode_loc = mode;
  // MeshCore answers a telemetry request at all only when the BASE permission is set,
  // so location would silently never be sent with the master switch off. Turning
  // sharing ON implies answering requests; say so via the note under the dropdown
  // rather than leaving the user with a setting that does nothing.
  if (mode != TELEM_MODE_DENY && _prefs->telemetry_mode_base == TELEM_MODE_DENY) {
    _prefs->telemetry_mode_base = TELEM_MODE_ALLOW_ALL;
  }
  platform::savePreferences();
}

void ConfigurationService::setExperimentalFlags(uint8_t multi_acks, uint8_t client_repeat, uint8_t rx_boosted) {
  if (!_prefs) return;
  _prefs->multi_acks = multi_acks ? 1u : 0u;
  _prefs->client_repeat = client_repeat ? 1u : 0u;
  _prefs->rx_boosted_gain = rx_boosted ? 1u : 0u;
  platform::savePreferences();
  // Base meshcomod branch does not expose applyRadioFromPrefs(); keep persisted only.
}
}
