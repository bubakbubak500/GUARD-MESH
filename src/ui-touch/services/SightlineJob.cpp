// SPDX-License-Identifier: GPL-3.0-or-later
#include "SightlineJob.h"
#include <cmath>
#include <cstring>
namespace ui {
bool SightlineJob::request(const Request &value) {
  if (_state.load(std::memory_order_acquire) != Idle || !value.id || !sightline::validPath(value.path) ||
      !value.server[0] || !std::memchr(value.server, 0, sizeof value.server))
    return false;
  _result = Result{};
  _result.request = value;
  _attempt.store(0, std::memory_order_relaxed);
  _state.store(Queued, std::memory_order_release);
  return true;
}
bool SightlineJob::take(Result &out) {
  if (_state.load(std::memory_order_acquire) != Ready)
    return false;
  out = _result;
  _state.store(Idle, std::memory_order_release);
  return true;
}
bool SightlineJob::active() const { return _state.load(std::memory_order_acquire) != Idle; }
bool SightlineJob::run(const Backend &backend) {
  State expected = Queued;
  if (!_state.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return false;
  for (int attempt = 1; attempt <= 3; ++attempt) {
    _attempt.store(attempt, std::memory_order_relaxed);
    for (auto &height : _result.elevations)
      height = NAN;
    _result.code = 0;
    int parsed =
        backend.fetch ? backend.fetch(backend.context, _result.request, _result.elevations, _result.code) : 0;
    _result.parsed = parsed >= 0 && parsed <= sightline::Samples ? parsed : 0;
    _result.valid = sightline::validElevations(_result.elevations, _result.parsed);
    _result.ok = sightline::repairElevations(_result.elevations, sightline::Samples, _result.parsed);
    if (_result.ok)
      break;
    if (attempt < 3 && backend.retryDelay)
      backend.retryDelay(backend.context);
  }
  _state.store(Ready, std::memory_order_release);
  return true;
}
} // namespace ui
