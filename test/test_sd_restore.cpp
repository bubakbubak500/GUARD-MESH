// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/SdRestoreJob.h"
#include "ui-touch/platform/StorageAccess.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace {
using Job = ui::services::SdRestoreJob;

struct Fixture {
  enum class Failure { None, NoCard, Prefs, Profile, Prepare, Arm, Copy };
  Failure failure = Failure::None;
  int busyOnCall = 0;
  int busyCalls = 0;
  int prefsCalls = 0;
  int heavyDepth = 0;
  int loopDepth = 0;
  int blocked = 0;
  int reboots = 0;
  int copies = 0;
  int delayed = 0;
  bool latchArmed = false;
  bool useSd = false;
  bool force = false;
  uint32_t floorWritten = 0;
  std::string events;

  void add(const char *event) {
    if (!events.empty()) events += ',';
    events += event;
  }
  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  static bool busy(void *context) {
    Fixture &f = self(context);
    f.add("busy");
    return ++f.busyCalls == f.busyOnCall;
  }
  static bool mount(void *context) {
    Fixture &f = self(context);
    assert(!ui::platform::storageAccess().admissionClosed());
    f.add("mount");
    return f.failure != Failure::NoCard;
  }
  static void heavyBegin(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 0);
    ++f.heavyDepth; f.add("H+");
  }
  static void heavyEnd(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1 && f.loopDepth == 0);
    --f.heavyDepth; f.add("H-");
  }
  static void history(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1);
    f.add("history");
  }
  static void discovered(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1);
    f.add("discovered");
  }
  static void contacts(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1);
    f.add("contacts");
  }
  static void syncHistory(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1);
    f.add("sync");
  }
  static bool prefs(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1);
    assert(!ui::platform::storageAccess().admissionClosed());
    // A deferred attempt repeats its pre-copy drain. Only the flush after
    // switching the backend runs inside the migration watchdog scope.
    assert(f.loopDepth == (f.useSd ? 1 : 0));
    ++f.prefsCalls;
    f.add("prefs");
    return f.failure != Failure::Prefs || f.prefsCalls > 1;
  }
  static bool match(void *context) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.add("match");
    return f.failure != Failure::Profile;
  }
  static bool prepare(void *context) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.add("prepare");
    return f.failure != Failure::Prepare;
  }
  static bool arm(void *context) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.add("arm");
    if (f.failure == Failure::Arm) return false;
    f.latchArmed = true;
    return true;
  }
  static Job::Status guardedMigration(void *context, Job::Status (*run)(void *), void *runContext) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1 && f.loopDepth == 0 && f.latchArmed);
    ++f.loopDepth; f.add("L+");
    const Job::Status status = run(runContext);
    assert(f.heavyDepth == 1 && f.loopDepth == 1);
    --f.loopDepth; f.add("L-");
    return status;
  }
  static bool migrate(void *context, bool forceCopy) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1 && f.loopDepth == 1 && f.latchArmed);
    assert(ui::platform::storageAccess().admissionClosed());
    ui::platform::StorageLease borrowed;
    assert(borrowed.acquired() && ui::platform::storageAccess().readerCount() == 0);
    bool otherAcquired = true;
    std::thread other([&] {
      ui::platform::StorageLease lease;
      otherAcquired = lease.acquired();
    });
    other.join();
    assert(!otherAcquired);
    ++f.copies; f.force = forceCopy; f.add("migrate");
    return f.failure != Failure::Copy;
  }
  static void markBlocked(void *context) {
    Fixture &f = self(context);
    ++f.blocked; f.add("blocked");
  }
  static void clear(void *context) {
    Fixture &f = self(context);
    assert(f.latchArmed && f.loopDepth == 1);
    assert(ui::platform::storageAccess().admissionClosed());
    f.latchArmed = false; f.add("clear");
  }
  static void setUseSd(void *context) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.useSd = true; f.add("useSd");
  }
  static uint32_t readFloor(void *context) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.add("clockRead");
    return 987654u;
  }
  static void setFloor(void *context, uint32_t floor) {
    Fixture &f = self(context);
    assert(ui::platform::storageAccess().admissionClosed());
    f.floorWritten = floor; f.add("clockSet");
  }
  static void delay(void *context, unsigned ms) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1 && f.loopDepth == 1);
    f.delayed = static_cast<int>(ms); f.add("delay");
  }
  static void reboot(void *context) {
    Fixture &f = self(context);
    assert(f.heavyDepth == 1 && f.loopDepth == 1);
    ++f.reboots; f.add("reboot");
  }
  Job::Host host() {
    Job::Host h;
    h.context = this;
    h.lifecycleBusy = busy; h.mountSd = mount;
    h.heavyWdtBegin = heavyBegin; h.heavyWdtEnd = heavyEnd;
    h.persistHistory = history; h.flushDiscovered = discovered;
    h.flushContacts = contacts; h.persistSyncHistory = syncHistory;
    h.flushPrefs = prefs; h.profileMatches = match;
    h.prepareMigration = prepare; h.armLatch = arm;
    h.guardedMigration = guardedMigration;
    h.migrate = migrate; h.markBlocked = markBlocked;
    h.clearLatch = clear; h.setUseSd = setUseSd;
    h.clockFloor = readFloor; h.setClockFloor = setFloor;
    h.delayMs = delay; h.reboot = reboot;
    return h;
  }
};

void admissionAndSuccess() {
  Fixture f;
  Job job(f.host(), Job::Policy(true, false));
  assert(job.tick() == Job::Status::Idle && f.events.empty());
  assert(job.request());
  assert(!job.request() && job.pending() && f.events.empty());
  f.busyOnCall = 1;
  assert(job.tick() == Job::Status::Deferred && job.pending() && f.events == "busy");
  f.busyOnCall = 0;
  f.events.clear();
  assert(job.tick() == Job::Status::RebootRequested && !job.pending());
  assert(f.events == "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,match,prepare,arm,L+,"
                     "migrate,clear,useSd,clockRead,clockSet,prefs,delay,reboot,L-,H-");
  assert(f.copies == 1 && !f.force && !f.latchArmed && f.useSd && f.floorWritten == 987654u);
  assert(f.prefsCalls == 2 && f.delayed == 400 && f.reboots == 1);
  assert(f.heavyDepth == 0 && f.loopDepth == 0);
  assert(!ui::platform::storageAccess().admissionClosed());
}

void concurrentReaderDefersBeforeLatch() {
  Fixture f;
  Job job(f.host(), Job::Policy(true, false));
  std::atomic<bool> readerReady{false}, releaseReader{false};
  std::thread reader([&] {
    ui::platform::StorageLease lease;
    assert(lease.acquired());
    readerReady.store(true, std::memory_order_release);
    while (!releaseReader.load(std::memory_order_acquire)) std::this_thread::yield();
  });
  while (!readerReady.load(std::memory_order_acquire)) std::this_thread::yield();
  assert(job.request());
  assert(job.tick() == Job::Status::Deferred && job.pending());
  assert(f.events == "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,H-");
  assert(!f.latchArmed && !f.copies && !f.reboots);
  assert(!ui::platform::storageAccess().admissionClosed());
  releaseReader.store(true, std::memory_order_release);
  reader.join();
  assert(job.tick() == Job::Status::RebootRequested && !job.pending());
  assert(f.copies == 1 && f.reboots == 1 && !ui::platform::storageAccess().admissionClosed());
}

void existingTransitionDefersBeforeDrains() {
  Fixture f;
  Job job(f.host(), Job::Policy(true, false));
  assert(job.request());
  {
    ui::platform::StorageTransition outer;
    assert(outer.requested() && outer.enter());
    assert(job.tick() == Job::Status::Deferred && job.pending() && f.events.empty());
  }
  assert(job.tick() == Job::Status::RebootRequested && !job.pending());
  assert(f.copies == 1 && !ui::platform::storageAccess().admissionClosed());
}

void secondAdmission() {
  Fixture f;
  Job job(f.host(), Job::Policy(true, false));
  assert(job.request());
  f.busyOnCall = 2;
  assert(job.tick() == Job::Status::Deferred && job.pending());
  assert(f.events == "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,H-");
  assert(f.copies == 0 && f.reboots == 0 && f.heavyDepth == 0);
  f.busyOnCall = 0;
  assert(job.tick() == Job::Status::RebootRequested && !job.pending());
  assert(f.copies == 1 && f.reboots == 1 && f.prefsCalls == 3);
}

void failures() {
  struct Case {
    Fixture::Failure failure;
    Job::Status status;
    int blocked;
    bool armed;
    const char *events;
  };
  const Case cases[] = {
      {Fixture::Failure::NoCard, Job::Status::NoCard, 0, false,
       "busy,mount"},
      {Fixture::Failure::Prefs, Job::Status::InternalBusy, 1, false,
       "busy,mount,H+,history,discovered,contacts,sync,prefs,blocked,H-"},
      {Fixture::Failure::Profile, Job::Status::ProfileMismatch, 0, false,
       "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,match,H-"},
      {Fixture::Failure::Prepare, Job::Status::PrepareFailed, 1, false,
       "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,match,prepare,blocked,H-"},
      {Fixture::Failure::Arm, Job::Status::LatchFailed, 1, false,
       "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,match,prepare,arm,blocked,H-"},
      {Fixture::Failure::Copy, Job::Status::CopyFailed, 1, true,
       "busy,mount,H+,history,discovered,contacts,sync,prefs,busy,match,prepare,arm,L+,migrate,"
       "blocked,L-,H-"},
  };
  for (const Case &test : cases) {
    Fixture f;
    f.failure = test.failure;
    Job job(f.host(), Job::Policy(true, false));
    assert(job.request());
    assert(job.tick() == test.status && !job.pending());
    assert(f.blocked == test.blocked && f.latchArmed == test.armed);
    assert(!f.useSd && f.reboots == 0 && f.heavyDepth == 0 && f.loopDepth == 0);
    assert(f.events == test.events);
    assert(!ui::platform::storageAccess().admissionClosed());
  }
}

void legacyAndInvalidHost() {
  Fixture f;
  f.failure = Fixture::Failure::Profile;
  Job legacy(f.host(), Job::Policy(false, true));
  assert(legacy.request());
  assert(legacy.tick() == Job::Status::RebootRequested);
  assert(f.copies == 1 && f.force && f.events.find("match") == std::string::npos);
  Fixture invalid;
  Job::Host h = invalid.host();
  h.armLatch = nullptr;
  Job missing(h, Job::Policy(true, false));
  assert(missing.request());
  assert(missing.tick() == Job::Status::InvalidHost && !missing.pending());
  assert(invalid.events.empty() && invalid.copies == 0 && invalid.reboots == 0);
  Job contradictory(invalid.host(), Job::Policy(true, true));
  assert(contradictory.request());
  assert(contradictory.tick() == Job::Status::InvalidHost && invalid.events.empty());
}
} // namespace

void sdRestoreRegression() {
  admissionAndSuccess();
  concurrentReaderDefersBeforeLatch();
  existingTransitionDefersBeforeDrains();
  secondAdmission();
  failures();
  legacyAndInvalidHost();
}
