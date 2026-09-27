// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/SdHealthMonitor.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string>

using ui::platform::StorageLease;
using ui::platform::StorageAccess;
using ui::platform::storageAccess;
using ui::services::SdHealthMonitor;

struct Fixture {
  bool adopt = false;
  bool probe = true;
  bool unmount = true;
  bool mount = true;
  bool backgroundMount = true;
  bool present = true;
  SdHealthMonitor::CardState card = SdHealthMonitor::CardState::Present;
  SdHealthMonitor::CardState cardStates[8] = {};
  unsigned cardCount = 0;
  unsigned cardIndex = 0;
  unsigned probes = 0;
  unsigned unmounts = 0;
  unsigned mounts = 0;
  unsigned backgroundMounts = 0;
  unsigned reconciles = 0;
  unsigned clears = 0;
  unsigned failureNotes = 0;
  unsigned missing = 0;
  unsigned adopted = 0;
  unsigned remounted = 0;
  unsigned lost = 0;
  unsigned removed = 0;
  unsigned healthy = 0;
  bool eventUnderGate = true;
  std::string order;

  void record(const char *value) {
    if (!order.empty()) order += ',';
    order += value;
  }
  static Fixture& self(void *p) { return *static_cast<Fixture *>(p); }
  static bool onAdopt(void *p) { Fixture &f = self(p); f.record("adopt"); return f.adopt; }
  static bool cardPresent(void *p) { return self(p).present; }
  static SdHealthMonitor::CardState cardState(void *p) {
    Fixture &f = self(p);
    return f.cardIndex < f.cardCount ? f.cardStates[f.cardIndex++] : f.card;
  }
  static bool probeAlive(void *p) {
    Fixture &f = self(p); ++f.probes; f.record("probe");
    assert(storageAccess().admissionClosed());
    return f.probe;
  }
  static bool onUnmount(void *p) {
    Fixture &f = self(p); ++f.unmounts; f.record("unmount");
    assert(storageAccess().admissionClosed());
    return f.unmount;
  }
  static bool onMount(void *p) {
    Fixture &f = self(p); ++f.mounts; f.record("mount");
    assert(storageAccess().admissionClosed());
    return f.mount;
  }
  static bool onBackgroundMount(void *p) {
    Fixture &f = self(p); ++f.backgroundMounts; f.record("backgroundMount");
    assert(storageAccess().admissionClosed());
    return f.backgroundMount;
  }
  static void clearFailure(void *p) { Fixture &f = self(p); ++f.clears; f.record("clear"); }
  static void noteFailure(void *p) { Fixture &f = self(p); ++f.failureNotes; f.record("failure"); }
  static void reconcile(void *p, bool removing) {
    Fixture &f = self(p); ++f.reconciles; f.record(removing ? "reconcile-removing" : "reconcile");
  }
  static void missingCard(void *p, uint32_t) { Fixture &f = self(p); ++f.missing; f.record("missing"); }
  static void event(void *p, SdHealthMonitor::Event event, uint32_t) {
    Fixture &f = self(p);
    if (event == SdHealthMonitor::Event::Adopted)
      assert(storageAccess().readerCount() > 0);
    StorageLease borrowed;
    assert(borrowed);
    const bool shouldBeExclusive = event != SdHealthMonitor::Event::Adopted;
    if (storageAccess().admissionClosed() != shouldBeExclusive) f.eventUnderGate = false;
    switch (event) {
      case SdHealthMonitor::Event::Adopted: ++f.adopted; f.record("event-adopted"); break;
      case SdHealthMonitor::Event::Remounted: ++f.remounted; f.record("event-remounted"); break;
      case SdHealthMonitor::Event::Lost: ++f.lost; f.record("event-lost"); break;
      case SdHealthMonitor::Event::Removed: ++f.removed; f.record("event-removed"); break;
      case SdHealthMonitor::Event::HealthyProbe: ++f.healthy; f.record("event-healthy"); break;
    }
  }
  SdHealthMonitor::Host host(bool withPager = false, bool withAdoption = false,
                             bool withMissing = true) {
    SdHealthMonitor::Host h;
    h.context = this;
    h.adoptLiveMount = withAdoption ? onAdopt : nullptr;
    h.cardPresent = withPager ? cardPresent : nullptr;
    h.readCardState = withPager ? cardState : nullptr;
    h.probeAlive = probeAlive;
    h.unmount = onUnmount;
    h.mount = onMount;
    h.backgroundMount = onBackgroundMount;
    h.clearFailureNote = clearFailure;
    h.noteFailure = noteFailure;
    h.reconcileBackend = reconcile;
    h.missingCard = withMissing ? missingCard : nullptr;
    h.event = event;
    return h;
  }
};

static SdHealthMonitor::Inputs mountedAt(uint32_t now) {
  SdHealthMonitor::Inputs in;
  in.now = now;
  in.mounted = true;
  return in;
}

static SdHealthMonitor::Inputs unmountedAt(uint32_t now) {
  SdHealthMonitor::Inputs in;
  in.now = now;
  in.adoptionAllowed = false;
  return in;
}

static void assertGateOpen() {
  assert(!storageAccess().admissionClosed());
  assert(storageAccess().readerCount() == 0);
}

static void adoptionAndOptionalCallbacks() {
  SdHealthMonitor monitor;
  Fixture f;
  f.adopt = true;
  auto in = unmountedAt(77);
  in.adoptionAllowed = true;
  monitor.tick(in, f.host(false, true));
  assert(f.order == "adopt,event-adopted");
  assert(f.adopted == 1 && f.reconciles == 0 && f.eventUnderGate);
  assertGateOpen();

  SdHealthMonitor background;
  Fixture b;
  in = unmountedAt(100);
  background.tick(in, b.host(false, false, false));
  assert(b.backgroundMounts == 1 && b.mounts == 0 && b.missing == 0);
  assert(b.order == "reconcile,clear,backgroundMount,event-remounted");
  assert(b.remounted == 1 && b.eventUnderGate);
  assertGateOpen();
}

static void readersDeferAndQueueTimesOut() {
  SdHealthMonitor monitor;
  Fixture f;
  StorageLease reader;
  assert(reader);
  const auto host = f.host();
  monitor.tick(unmountedAt(100), host);
  assert(f.backgroundMounts == 0 && monitor.removalPending() == false);
  assert(storageAccess().admissionClosed());
  reader.release();
  monitor.tick(unmountedAt(101), host);
  assert(f.backgroundMounts == 1 && f.remounted == 1);
  assertGateOpen();

  SdHealthMonitor queued;
  Fixture q;
  int token = 0;
  StorageAccess::Transition owner;
  assert(owner.request(storageAccess(), reinterpret_cast<uintptr_t>(&token)) && owner.enter());
  const auto queuedHost = q.host();
  queued.tick(unmountedAt(1000), queuedHost);
  queued.tick(unmountedAt(16000), queuedHost);
  assert(q.backgroundMounts == 0 && !queued.removalPending());
  assert(owner.ready() && storageAccess().admissionClosed());
  owner.release();
  auto afterTimeout = unmountedAt(45999);
  queued.tick(afterTimeout, queuedHost);
  assert(q.backgroundMounts == 0);
  afterTimeout.now = 46000;
  queued.tick(afterTimeout, queuedHost);
  assert(q.backgroundMounts == 1);
  assertGateOpen();
}

static void cancellationReleasesDeferredOwners() {
  for (int mode = 0; mode < 3; ++mode) {
    SdHealthMonitor monitor;
    Fixture f;
    auto host = f.host();
    StorageLease reader;
    assert(reader);
    auto in = unmountedAt(100);
    monitor.tick(in, host);
    assert(storageAccess().admissionClosed() && f.backgroundMounts == 0);
    reader.release();
    if (mode == 0) in.formatPending = true;
    else if (mode == 1) in.sleeping = true;
    else in.fileManagerActive = true;
    in.now = 101;
    monitor.tick(in, host);
    assert(!storageAccess().admissionClosed() && f.backgroundMounts == 0);
    assertGateOpen();
  }
}

static void probesAndFailurePaths() {
  SdHealthMonitor monitor;
  Fixture f;
  const auto host = f.host();
  auto in = mountedAt(100);
  in.sleeping = true;
  in.failureNoted = true;
  monitor.tick(in, host);
  assert(f.probes == 1 && f.healthy == 1 && f.unmounts == 0 && f.mounts == 0);
  assert(f.order == "reconcile,clear,probe,event-healthy");
  assert(f.eventUnderGate);

  in.now = 101;
  in.failureNoted = false;
  monitor.tick(in, host);
  assert(f.probes == 1); // background probes pause while asleep

  in.now = 5099;
  in.failureNoted = true;
  monitor.tick(in, host);
  assert(f.probes == 1); // failure probe waits for the five-second deadline

  in.now = 5100;
  f.probe = false;
  f.mount = false;
  monitor.tick(in, host);
  assert(f.probes == 2 && f.unmounts == 1 && f.mounts == 1 && f.lost == 1);
  assert(f.order.find("unmount,mount,event-lost") != std::string::npos);
  assert(f.eventUnderGate);
  assertGateOpen();

  SdHealthMonitor successful;
  Fixture s;
  s.probe = false;
  auto active = mountedAt(50);
  active.failureNoted = true;
  successful.tick(active, s.host());
  assert(s.unmounts == 1 && s.mounts == 1 && s.remounted == 1 && s.lost == 0);
  assert(s.order == "reconcile,clear,probe,unmount,mount,event-remounted");
  assertGateOpen();

  SdHealthMonitor background;
  Fixture bg;
  auto idle = mountedAt(100);
  background.tick(idle, bg.host());
  assert(bg.probes == 1);
  idle.now = 30099;
  background.tick(idle, bg.host());
  assert(bg.probes == 1);
  idle.now = 30100;
  background.tick(idle, bg.host());
  assert(bg.probes == 2);
  assertGateOpen();
}

static void pagerDebounceAndPendingRemoval() {
  SdHealthMonitor monitor;
  Fixture f;
  f.cardStates[0] = SdHealthMonitor::CardState::Absent;
  f.cardStates[1] = SdHealthMonitor::CardState::Unknown;
  f.cardStates[2] = SdHealthMonitor::CardState::Absent;
  f.cardStates[3] = SdHealthMonitor::CardState::Absent;
  f.cardStates[4] = SdHealthMonitor::CardState::Absent;
  f.cardCount = 5;
  const auto host = f.host(true);
  auto in = mountedAt(10);
  monitor.tick(in, host); // First absent sample.
  in.now = 510; monitor.tick(in, host); // Unknown resets the absent sample.
  in.now = 1010; monitor.tick(in, host); // Absent starts a fresh debounce.
  assert(f.failureNotes == 0 && f.unmounts == 0);
  in.now = 2510; monitor.tick(in, host); // Gap > 1100 ms restarts it again.
  assert(f.failureNotes == 0 && f.unmounts == 0);

  StorageLease reader;
  assert(reader);
  in.now = 3010;
  monitor.tick(in, host); // Second absent sample requests removal but reader blocks it.
  assert(monitor.removalPending() && f.failureNotes == 1 && f.unmounts == 0);
  monitor.cancel();
  assert(monitor.removalPending()); // cancel releases admission only
  reader.release();
  in.now = 3011;
  monitor.tick(in, host);
  assert(!monitor.removalPending() && f.unmounts == 1 && f.removed == 1);
  assert(f.eventUnderGate);
  assertGateOpen();
}

int main() {
  adoptionAndOptionalCallbacks();
  readersDeferAndQueueTimesOut();
  cancellationReleasesDeferredOwners();
  probesAndFailurePaths();
  pagerDebounceAndPendingRemoval();
}
