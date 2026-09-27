// SPDX-License-Identifier: GPL-3.0-or-later
#include "WifiScanJob.h"
#include <cstring>
namespace ui {
bool WifiScanJob::Snapshot::add(const char *ssid) {
  if (!ssid || !*ssid || count < 0 || count >= Capacity)
    return false;
  char bounded[SsidCapacity]{};
  strncpy(bounded, ssid, sizeof bounded - 1);
  for (int i = 0; i < count; ++i)
    if (!strcmp(ssids[i], bounded))
      return false;
  memcpy(ssids[count++], bounded, sizeof bounded);
  return true;
}
bool WifiScanJob::request() {
  if (_state.load(std::memory_order_acquire) != Idle)
    return false;
  _state.store(Queued, std::memory_order_release);
  return true;
}
bool WifiScanJob::cancel() {
  State expected = Queued;
  return _state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel);
}
void WifiScanJob::failQueued() {
  State expected = Queued;
  if (!_state.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return;
  _result = Snapshot{};
  _state.store(Ready, std::memory_order_release);
}
bool WifiScanJob::active() const { return _state.load(std::memory_order_acquire) != Idle; }
bool WifiScanJob::poll() {
  if (_state.load(std::memory_order_acquire) != Ready)
    return false;
  _snapshot = _result;
  _state.store(Idle, std::memory_order_release);
  return true;
}
bool WifiScanJob::run(void *context, Scan scan) {
  State expected = Queued;
  if (!_state.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return false;
  _result = Snapshot{};
  if (scan)
    scan(context, _result);
  if (_result.count < 0)
    _result.count = 0;
  if (_result.count > Capacity)
    _result.count = Capacity;
  for (auto &ssid : _result.ssids)
    ssid[SsidCapacity - 1] = 0;
  _state.store(Ready, std::memory_order_release);
  return true;
}
} // namespace ui
