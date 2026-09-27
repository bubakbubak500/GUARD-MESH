// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/StorageMaintenance.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>

using ui::platform::StorageLease;
using ui::platform::StorageTransition;
using ui::platform::storageAccess;
using ui::services::StorageMaintenance;

static void drainAndScopedOperation() {
  StorageMaintenance maintenance;
  StorageLease inFlight;
  assert(inFlight && storageAccess().readerCount() == 1);
  {
    StorageMaintenance::Attempt pending(maintenance, 100, false);
    assert(pending.status() == StorageMaintenance::Status::Deferred);
    assert(maintenance.pending() && storageAccess().admissionClosed());
    StorageLease lateWorker;
    assert(!lateWorker); // its queued work must retry after the operation
  }
  assert(maintenance.pending() && storageAccess().readerCount() == 1);
  inFlight.release();
  {
    StorageMaintenance::Attempt operation(maintenance, 101, false);
    assert(operation.ready());
    StorageLease nestedFilesystemCall;
    assert(nestedFilesystemCall && storageAccess().readerCount() == 0);
    StorageMaintenance::Attempt nestedAttempt(maintenance, 101, false);
    assert(nestedAttempt.status() == StorageMaintenance::Status::Deferred);
    assert(operation.ready() && storageAccess().admissionClosed());
  }
  assert(!maintenance.pending() && !storageAccess().admissionClosed());
  StorageLease retried;
  assert(retried);
}

static void wrapSafeTimeoutAndCancel() {
  StorageMaintenance maintenance;
  StorageLease inFlight;
  assert(inFlight);
  const uint32_t start = UINT32_MAX - 4;
  {
    StorageMaintenance::Attempt pending(maintenance, start, false, 7);
    assert(pending.status() == StorageMaintenance::Status::Deferred);
  }
  {
    StorageMaintenance::Attempt pending(maintenance, 1, false, 7);
    assert(pending.status() == StorageMaintenance::Status::Deferred);
  }
  {
    StorageMaintenance::Attempt timedOut(maintenance, 2, false, 7);
    assert(timedOut.status() == StorageMaintenance::Status::TimedOut);
  }
  assert(!maintenance.pending() && !storageAccess().admissionClosed());
  assert(storageAccess().readerCount() == 1); // cancellation never ends active I/O
  StorageLease afterCancel;
  assert(afterCancel);
  afterCancel.release();
  inFlight.release();
}

static void competingOwnersAndLegacyBusy() {
  StorageMaintenance first;
  StorageMaintenance second;
  StorageLease inFlight;
  assert(inFlight);
  {
    StorageMaintenance::Attempt one(first, 10, false);
    assert(one.status() == StorageMaintenance::Status::Deferred);
    StorageMaintenance::Attempt two(second, 10, false);
    assert(two.status() == StorageMaintenance::Status::Deferred);
    assert(first.pending() && second.pending());
  }
  first.cancel();
  assert(!storageAccess().admissionClosed());
  inFlight.release();
  {
    StorageMaintenance::Attempt two(second, 11, true);
    assert(two.status() == StorageMaintenance::Status::Deferred);
    assert(storageAccess().admissionClosed());
    StorageLease sameThreadButPending;
    assert(!sameThreadButPending); // ready reservation is not an active operation
  }
  {
    StorageMaintenance::Attempt two(second, 12, false);
    assert(two.ready());
  }
  assert(!storageAccess().admissionClosed());
}

static bool failDuringMaintenance(StorageMaintenance& maintenance) {
  StorageMaintenance::Attempt operation(maintenance, 50, false);
  if (!operation.ready()) return false;
  StorageLease nested;
  assert(nested);
  return false; // destructor still releases the active reservation
}

static void earlyReturnAndExplicitCancel() {
  StorageMaintenance maintenance;
  assert(!failDuringMaintenance(maintenance));
  assert(!storageAccess().admissionClosed());
  StorageLease nextOperation;
  assert(nextOperation);
  nextOperation.release();
  {
    StorageMaintenance::Attempt pending(maintenance, 60, true);
    assert(pending.status() == StorageMaintenance::Status::Deferred);
  }
  assert(maintenance.pending());
  maintenance.cancel();
  assert(!maintenance.pending() && !storageAccess().admissionClosed());
}

static void competingActiveTransitionTimesOut() {
  StorageTransition other;
  assert(other.ready() && other.enter());
  StorageMaintenance maintenance;
  {
    StorageMaintenance::Attempt pending(maintenance, 100, false, 5);
    assert(pending.status() == StorageMaintenance::Status::Deferred);
  }
  {
    StorageMaintenance::Attempt timedOut(maintenance, 105, false, 5);
    assert(timedOut.status() == StorageMaintenance::Status::TimedOut);
  }
  assert(other.ready() && storageAccess().admissionClosed());
  other.release();
  assert(!storageAccess().admissionClosed());
}

int main() {
  drainAndScopedOperation();
  wrapSafeTimeoutAndCancel();
  competingOwnersAndLegacyBusy();
  earlyReturnAndExplicitCancel();
  competingActiveTransitionTimesOut();
}
