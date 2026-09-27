// SPDX-License-Identifier: GPL-3.0-or-later
#include "StorageMaintenance.h"

namespace ui { namespace services {

StorageMaintenance::Attempt::Attempt(StorageMaintenance& maintenance, uint32_t now,
                                     bool legacyBusy, uint32_t timeoutMs)
    : _maintenance(maintenance) {
  // Reentry may happen through a filesystem callback in the active lifecycle
  // operation. It must never cancel or release that outer operation.
  if (_maintenance._active) return;

  const bool firstAttempt = !_maintenance._started;
  if (firstAttempt) {
    _maintenance._started = true;
    _maintenance._startedAt = now;
  }
  const uintptr_t context = platform::currentStorageContext();
  if (!_maintenance._transition.requested()) {
    // StorageAccess permits same-task borrowing for nested filesystem helpers.
    // A separate maintenance owner must acquire its own reservation instead.
    if (!platform::storageAccess().admissionClosed() &&
        _maintenance._transition.request())
      _maintenance._context = context;
  }

  // A persistent reservation belongs to the task that made it. A later tick
  // on another task cannot enter it even if all readers have drained.
  const bool ownContext = _maintenance._transition.requested() &&
                          _maintenance._context == context;
  const bool expired = uint32_t(now - _maintenance._startedAt) >= timeoutMs;
  // A deadline reached on a later tick wins over newly observed readiness.
  // An immediately available first attempt may still run with timeoutMs=0.
  if (expired && (!firstAttempt || legacyBusy || !ownContext ||
                  !_maintenance._transition.ready())) {
    _maintenance.cancel();
    _status = Status::TimedOut;
    return;
  }
  if (ownContext && !legacyBusy && _maintenance._transition.enter()) {
    _maintenance._active = true;
    _ownsActive = true;
    _status = Status::Ready;
    return;
  }
  if (expired) {
    _maintenance.cancel();
    _status = Status::TimedOut;
  }
}

StorageMaintenance::Attempt::~Attempt() {
  if (_ownsActive) _maintenance.finishActive();
}

void StorageMaintenance::cancel() {
  if (_active) return;
  _transition.release();
  _startedAt = 0;
  _context = 0;
  _started = false;
}

void StorageMaintenance::finishActive() {
  _active = false;
  cancel();
}

} } // namespace ui::services
