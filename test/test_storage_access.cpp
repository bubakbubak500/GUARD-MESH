// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/platform/StorageAccess.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

using ui::platform::StorageAccess;

static void admissionAndNesting() {
  StorageAccess access;
  StorageAccess::Reader first(access, 11);
  StorageAccess::Reader second(access, 22);
  assert(first && second && access.readerCount() == 2);

  StorageAccess::Transition owner;
  assert(owner.request(access, 31));
  assert(owner.requested() && access.admissionClosed() && !owner.ready());
  assert(owner.request(access, 31)); // repeated tick retains one reservation
  assert(!owner.request(access, 32));
  StorageAccess::Reader queuedWorker(access, 22);
  assert(!queuedWorker && access.readerCount() == 2);
  StorageAccess::Reader earlyOwnerRead(access, 31);
  StorageAccess::Transition earlyNested(access, 31);
  assert(!earlyOwnerRead && !earlyNested.requested());
  StorageAccess::Transition competitor(access, 41);
  assert(!competitor.requested());

  first.release();
  first.release();
  assert(access.readerCount() == 1 && !owner.ready());
  second.release();
  assert(access.readerCount() == 0 && owner.ready());
  StorageAccess::Reader idleOwnerRead(access, 31);
  StorageAccess::Transition idleNested(access, 31);
  assert(!idleOwnerRead && !idleNested.requested());
  assert(owner.enter() && owner.enter());
  {
    StorageAccess::Reader borrowedRead(access, 31);
    StorageAccess::Transition borrowedTransition(access, 31);
    StorageAccess::Reader stranger(access, 41);
    StorageAccess::Transition otherWriter(access, 41);
    assert(borrowedRead && borrowedTransition.ready() && borrowedTransition.enter());
    assert(!stranger && !otherWriter.requested());
    assert(access.readerCount() == 0 && access.admissionClosed());
    borrowedRead.release();
    borrowedTransition.release();
    assert(owner.ready() && access.admissionClosed());
  }
  owner.release();
  assert(!owner.requested() && !access.admissionClosed());
  StorageAccess::Reader retry(access, 22);
  assert(retry && access.readerCount() == 1);
  retry.release();
}

static void cancellationAndRetry() {
  StorageAccess access;
  StorageAccess::Transition persistent;
  StorageAccess::Reader inFlight(access, 9);
  assert(inFlight);
  assert(persistent.request(access, 1) && !persistent.ready());
  persistent.release(); // cancel while an admitted reader is still in I/O
  assert(!access.admissionClosed() && access.readerCount() == 1);
  StorageAccess::Reader later(access, 2);
  assert(later && access.readerCount() == 2);
  later.release();
  assert(persistent.request(access, 1) && !persistent.ready());
  StorageAccess::Reader deferred(access, 2);
  assert(!deferred); // failed work remains eligible for another attempt
  inFlight.release();
  assert(persistent.ready());
  assert(persistent.enter());
  persistent.release();
  assert(persistent.request(access, 1) && persistent.ready());
  assert(persistent.enter());
  persistent.release();
  StorageAccess::Reader resumed(access, 2);
  assert(resumed);
}

static void concurrentDrainAndAdmission() {
  StorageAccess access;
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool exitReader = false;
  std::thread reader([&] {
    StorageAccess::Reader active(access, ui::platform::currentStorageContext());
    assert(active);
    {
      std::lock_guard<std::mutex> lock(mutex);
      entered = true;
    }
    changed.notify_all();
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return exitReader; });
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return entered; });
  }
  StorageAccess::Transition writer(access, ui::platform::currentStorageContext());
  assert(writer.requested() && !writer.ready());
  std::thread contender([&] {
    const uintptr_t context = ui::platform::currentStorageContext();
    StorageAccess::Reader denied(access, context);
    StorageAccess::Transition deniedWriter(access, context);
    assert(!denied && !deniedWriter.requested());
  });
  contender.join();
  assert(access.readerCount() == 1 && !writer.ready());
  {
    std::lock_guard<std::mutex> lock(mutex);
    exitReader = true;
  }
  changed.notify_all();
  reader.join();
  assert(writer.ready() && access.readerCount() == 0);
  StorageAccess::Reader ownButIdle(access, ui::platform::currentStorageContext());
  assert(!ownButIdle);
  assert(writer.enter());
  writer.release();
  assert(!access.admissionClosed());
  StorageAccess::Reader admitted(access, ui::platform::currentStorageContext());
  assert(admitted);
}

static void singletonWrappers() {
  using ui::platform::StorageLease;
  using ui::platform::StorageTransition;
  StorageTransition persistent(false);
  assert(!persistent.requested());
  StorageLease first;
  assert(first);
  assert(persistent.request() && !persistent.ready());
  first.release();
  assert(persistent.ready());
  StorageLease idleOwnerRead;
  assert(!idleOwnerRead);
  assert(persistent.enter());
  StorageLease nested;
  assert(nested && ui::platform::storageAccess().readerCount() == 0);
  nested.release();
  persistent.release();
  assert(!ui::platform::storageAccess().admissionClosed());
}

int main() {
  admissionAndNesting();
  cancellationAndRetry();
  concurrentDrainAndAdmission();
  singletonWrappers();
}
