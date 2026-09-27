// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdateJobs.h"
#include "../platform/StorageAccess.h"
#include <climits>
#include <cstdio>
#include <cstring>
namespace ui {
bool FirmwareUpdateJobs::requestCheck(bool beta, uint32_t generation) {
  if (checkActive())
    return false;
  _check = {{beta, generation}, -1};
  _checkState.store(Queued, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::checkActive() const { return _checkState.load(std::memory_order_acquire) != Idle; }
bool FirmwareUpdateJobs::takeCheck(CheckResult &result) {
  if (_checkState.load(std::memory_order_acquire) != Ready)
    return false;
  result = _check;
  _checkState.store(Idle, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::requestInstall(Destination destination, bool beta, int version) {
  if (version < 0 || installActive())
    return false;
  _install = {{destination, beta, version}, false, {0}};
  _progress.store(0, std::memory_order_relaxed);
  _installState.store(Queued, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::installActive() const {
  return _installState.load(std::memory_order_acquire) != Idle;
}
bool FirmwareUpdateJobs::storageBusy() const {
  const auto state = _installState.load(std::memory_order_acquire);
  return (state == Queued || state == Running) && _install.request.destination == Destination::Sd;
}
int FirmwareUpdateJobs::installState(Destination destination) const {
  const auto state = _installState.load(std::memory_order_acquire);
  if (state == Idle || _install.request.destination != destination)
    return 0;
  return state == Ready ? (_install.ok ? 2 : 3) : 1;
}
bool FirmwareUpdateJobs::takeInstall(Destination destination, InstallResult &result) {
  if (_installState.load(std::memory_order_acquire) != Ready || _install.request.destination != destination)
    return false;
  result = _install;
  _installState.store(Idle, std::memory_order_release);
  return true;
}
void FirmwareUpdateJobs::failQueuedCheck() {
  State expected = Queued;
  if (!_checkState.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return;
  _check.latest = -1;
  _checkState.store(Ready, std::memory_order_release);
}
void FirmwareUpdateJobs::failQueuedInstall(const char *message) {
  State expected = Queued;
  if (!_installState.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return;
  _install.ok = false;
  snprintf(_install.message, sizeof _install.message, "%s", message ? message : "Not enough memory");
  _installState.store(Ready, std::memory_order_release);
}
bool FirmwareUpdateJobs::runCheck(const Backend &backend) {
  State expected = Queued;
  if (!_checkState.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return false;
  _check.latest = backend.check ? backend.check(backend.context, _check.request.beta) : -1;
  _checkState.store(Ready, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::runInstall(const Backend &backend) {
  // The queued request is published with the state. Reserve storage before
  // claiming it, so lifecycle shutdown leaves the request and progress intact.
  platform::StorageLease lease;
  if (!lease.acquired()) return false;
  State expected = Queued;
  if (!_installState.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return false;
  // The backend receives its own result. UI queries read only the immutable request
  // until Ready, so even a backend assigning the complete result cannot race them.
  InstallResult result{_install.request, false, {0}};
  Progress progress{
      this, [](void *context, int percent) {
        auto &self = *static_cast<FirmwareUpdateJobs *>(context);
        self._progress.store(percent < 0 ? 0 : percent > 100 ? 100 : percent, std::memory_order_relaxed);
      }};
  if (backend.install)
    backend.install(backend.context, _install.request, progress, result);
  else
    snprintf(result.message, sizeof result.message, "Update unavailable");
  _install.ok = result.ok;
  memcpy(_install.message, result.message, sizeof _install.message);
  _install.message[sizeof _install.message - 1] = 0;
  if (_install.ok)
    _progress.store(100, std::memory_order_relaxed);
  _installState.store(Ready, std::memory_order_release);
  return true;
}
bool ReleaseMonitor::selectChannel(bool beta) {
  if (beta == _beta)
    return false;
  _beta = beta;
  ++_generation;
  _latest = -1;
  _checked = false;
  _force = true;
  _failures = 0;
  return true;
}
bool ReleaseMonitor::tick(FirmwareUpdateJobs &jobs, uint32_t now, bool networkReady, int currentVersion,
                          bool (*ensureExecutor)()) {
  if (currentVersion < 0)
    return false;
  bool changed = false;
  FirmwareUpdateJobs::CheckResult result;
  if (jobs.takeCheck(result) && result.request.generation == _generation && result.request.beta == _beta) {
    _latest = result.latest;
    _checked = true;
    changed = true;
    if (_latest < 0) {
      if (_failures < 10)
        ++_failures;
      _next = now + (_failures <= 3 ? 60000u : 300000u);
    } else {
      _failures = 0;
      _next = now + 6u * 60u * 60u * 1000u;
    }
  }
  if (networkReady && !jobs.checkActive() &&
      (_force || !_scheduled || static_cast<int32_t>(now - _next) >= 0)) {
    if (jobs.requestCheck(_beta, _generation)) {
      _force = false;
      _scheduled = true;
      _next = now + 6u * 60u * 60u * 1000u;
      if (!ensureExecutor || !ensureExecutor())
        jobs.failQueuedCheck();
    }
  }
  return changed;
}
void ReleaseListing::feed(char c) {
  static const char pattern[] = "beta_";
  if (_digits) {
    if (c >= '0' && c <= '9') {
      if (_number < 0)
        _number = 0;
      if (_number > (INT_MAX - (c - '0')) / 10)
        _overflow = true;
      if (!_overflow)
        _number = _number * 10 + c - '0';
      return;
    }
    if (!_overflow && _number > _best)
      _best = _number;
    _digits = false;
    _number = -1;
    _matched = 0;
    _overflow = false;
  }
  if (c == pattern[_matched]) {
    if (++_matched == sizeof pattern - 1) {
      _matched = 0;
      _digits = true;
    }
  } else
    _matched = c == pattern[0] ? 1 : 0;
}
int ReleaseListing::latest() const { return _digits && !_overflow && _number > _best ? _number : _best; }
int ReleaseListing::version(const char *tag) {
  if (!tag || strncmp(tag, "beta_", 5) || tag[5] < '0' || tag[5] > '9')
    return -1;
  int number = 0;
  for (const char *p = tag + 5; *p; ++p) {
    if (*p < '0' || *p > '9' || number > (INT_MAX - (*p - '0')) / 10)
      return -1;
    number = number * 10 + *p - '0';
  }
  return number;
}
} // namespace ui
