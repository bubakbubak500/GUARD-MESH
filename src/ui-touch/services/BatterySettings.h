// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/BatteryModel.h"
#include <stddef.h>
namespace ui {
// UI-thread service. Owns the voltage filter, published reading, preferences and
// a cancellable calibration burst. tick samples at most once; it never sleeps.
class BatterySettings {
public:
  struct Host {
    void *context;
    uint16_t (*readMv)(void *);
    int (*stateOfCharge)(void *); // -1 means no hardware fuel-gauge reading
    void (*setSleep)(void *, bool);
    void (*sleepBlocker)(void *, char *, size_t);
    bool smooth;
    bool supportsSleep;
  };
  enum class CalibrationState { Idle, Sampling, Calibrated, TooLow, Reset, SaveFailed };
  struct Calibration {
    uint32_t id = 0;
    CalibrationState state = CalibrationState::Idle;
    uint16_t millivolts = 0;
  };
  struct SleepState {
    bool supported = false, enabled = false;
    char blocker[192]{};
  };
  explicit BatterySettings(Host host) : _host(host) {}
  BatterySettings(const BatterySettings &) = delete;
  BatterySettings &operator=(const BatterySettings &) = delete;
  uint16_t fullMv();
  uint16_t publishedMv(uint32_t now);
  bool isCharging(uint16_t mv);
  int percent(uint16_t mv);
  SleepState sleepState() const;
  bool setSleep(bool enabled);
  uint32_t beginCalibration(uint32_t now);
  void cancelCalibration(uint32_t id);
  bool resetCalibration();
  void tick(uint32_t now);
  Calibration calibration() const { return _calibration; }

private:
  uint16_t sample();
  uint32_t nextId();
  bool saveCalibration(uint16_t mv);
  Host _host;
  BatteryModel _model;
  Calibration _calibration;
  uint32_t _nextSample = 0, _sum = 0;
  unsigned _samples = 0, _validSamples = 0;
  bool _sampling = false;
};
} // namespace ui
