// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdateJobs.h"
#include "../platform/StorageAccess.h"
#include <cstdio>
#include <cstring>
namespace ui {
bool FirmwareUpdateJobs::requestCheck(bool beta, uint32_t generation) {
  if (checkActive() || installActive())
    return false;
  _check = {{beta, generation}, -1, {}, false, {0}};
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
  if (version < 0 || installActive() || checkActive())
    return false;
  _install = {{destination, beta, version, {}}, false, {0}};
  _progress.store(0, std::memory_order_relaxed);
  _installState.store(Queued, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::requestInstall(const FirmwareRelease &release) {
  if (installActive() || checkActive() || !release.size) return false;
  _install = {{Destination::Ota, false, 0, release}, false, {0}};
  _progress.store(0, std::memory_order_relaxed);
  _installState.store(Queued, std::memory_order_release);
  return true;
}
bool FirmwareUpdateJobs::installActive() const {
  return _installState.load(std::memory_order_acquire) != Idle;
}
bool FirmwareUpdateJobs::storageBusy() const {
  const auto state = _installState.load(std::memory_order_acquire);
  return state == Queued || state == Running;
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
  snprintf(_check.message, sizeof _check.message, "Update worker unavailable");
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
  if (backend.resolve) backend.resolve(backend.context, _check);
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
} // namespace ui
