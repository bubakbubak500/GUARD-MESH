// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../NodePrefs.h"
namespace ui {
class ConfigurationService {
public:
  explicit ConfigurationService(NodePrefs* prefs) : _prefs(prefs) {}
  bool setNodeName(const char* s);
  bool setRadioParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_dbm, float airtime_factor);
  void setAutoAddConfig(uint8_t mask, uint8_t max_hops, uint8_t manual_add);
  void setAdvertLocationPolicy(uint8_t policy);
  void setPathHashMode(uint8_t mode);
  void setTelemetryAllow(bool on);
  void setLocationTelemetryMode(uint8_t mode);
  void setExperimentalFlags(uint8_t multi_acks, uint8_t client_repeat, uint8_t rx_boosted);
private:
  NodePrefs* _prefs;
};
}
