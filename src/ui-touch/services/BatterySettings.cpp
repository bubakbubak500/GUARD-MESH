// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatterySettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui {
uint16_t BatterySettings::fullMv() {
  if (!_model.calibrationLoaded())
    _model.setFullMv(touchPrefsGetBattFullMv());
  return _model.fullMv();
}
uint16_t BatterySettings::sample() {
  return _model.sample(_host.readMv ? _host.readMv(_host.context) : 0, _host.smooth);
}
uint16_t BatterySettings::publishedMv(uint32_t now) {
  fullMv();
  return _model.publish(sample(), now);
}
bool BatterySettings::isCharging(uint16_t mv) {
  fullMv();
  return _model.isCharging(mv);
}
int BatterySettings::percent(uint16_t mv) {
  if (_host.stateOfCharge) {
    const int percent = _host.stateOfCharge(_host.context);
    if (percent >= 0 && percent <= 100)
      return percent;
  }
  fullMv();
  return _model.percent(mv);
}
BatterySettings::SleepState BatterySettings::sleepState() const {
  SleepState state;
  state.supported = _host.supportsSleep;
  if (state.supported) {
    state.enabled = touchPrefsGetSleepIdle();
    if (_host.sleepBlocker)
      _host.sleepBlocker(_host.context, state.blocker, sizeof state.blocker);
    state.blocker[sizeof state.blocker - 1] = 0;
  }
  return state;
}
bool BatterySettings::setSleep(bool enabled) {
  if (!_host.supportsSleep)
    return false;
  const bool saved = touchPrefsSetSleepIdle(enabled);
  // The preference store updates its RAM snapshot even on persistence failure.
  // Keep the live setting consistent with it, but report the failed save.
  if (_host.setSleep)
    _host.setSleep(_host.context, enabled);
  return saved;
}
uint32_t BatterySettings::nextId() {
  if (++_calibration.id == 0)
    ++_calibration.id;
  return _calibration.id;
}
uint32_t BatterySettings::beginCalibration(uint32_t now) {
  nextId();
  _calibration.state = CalibrationState::Sampling;
  _calibration.millivolts = 0;
  _nextSample = now;
  _sum = _samples = _validSamples = 0;
  return _calibration.id;
}
void BatterySettings::cancelCalibration(uint32_t id) {
  if (id && id == _calibration.id && _calibration.state == CalibrationState::Sampling) {
    nextId();
    _calibration.state = CalibrationState::Idle;
  }
}
bool BatterySettings::saveCalibration(uint16_t mv) {
  const bool saved = touchPrefsSetBattFullMv(mv);
  _model.setFullMv(mv);
  return saved;
}
bool BatterySettings::resetCalibration() {
  nextId();
  _calibration.millivolts = 0;
  const bool saved = saveCalibration(0);
  _calibration.state = saved ? CalibrationState::Reset : CalibrationState::SaveFailed;
  return saved;
}
void BatterySettings::tick(uint32_t now) {
  if (_sampling || _calibration.state != CalibrationState::Sampling || int32_t(now - _nextSample) < 0)
    return;
  const auto id = _calibration.id;
  // A hardware adapter may reenter UI code. A cancelled/replaced request must
  // never incorporate the reading or save the old calibration afterwards.
  _sampling = true;
  const auto mv = sample();
  _sampling = false;
  if (_calibration.id != id || _calibration.state != CalibrationState::Sampling)
    return;
  _nextSample = now + 20u;
  if (mv) {
    _sum += mv;
    ++_validSamples;
  }
  if (++_samples < 8)
    return;
  _calibration.millivolts = _validSamples ? _sum / _validSamples : 0;
  if (_calibration.millivolts < 3500)
    _calibration.state = CalibrationState::TooLow;
  else
    _calibration.state = saveCalibration(_calibration.millivolts) ? CalibrationState::Calibrated
                                                                  : CalibrationState::SaveFailed;
}
} // namespace ui
