// SPDX-License-Identifier: GPL-3.0-or-later
#include "SdHealthMonitor.h"
#include "../platform/StorageAccess.h"

namespace ui { namespace services {

bool SdHealthMonitor::validHost(const Host& host) const {
  return host.probeAlive && host.unmount && host.mount && host.backgroundMount &&
         host.clearFailureNote && host.noteFailure && host.reconcileBackend &&
         host.event;
}

uint32_t SdHealthMonitor::deadline(uint32_t now, uint32_t interval) {
  return now + interval;
}

void SdHealthMonitor::emit(const Host& host, Event event, uint32_t now) {
  host.event(host.context, event, now);
}

void SdHealthMonitor::cancel() {
  _maintenance.cancel();
}

void SdHealthMonitor::tick(const Inputs& inputs, const Host& host) {
  if (!validHost(host)) {
    cancel();
    return;
  }
  if (inputs.formatPending) {
    cancel();
    return;
  }

  if (!_maintenance.pending() && !inputs.mounted && inputs.adoptionAllowed &&
      host.adoptLiveMount) {
    platform::StorageLease liveMount;
    if (liveMount && host.adoptLiveMount(host.context)) {
      emit(host, Event::Adopted, inputs.now);
      return;
    }
  }

  // The adapter retains UITask's tile-worker and filesystem checks. `removing`
  // tells it to defer the mounted-card swap while Pager removal is pending.
  host.reconcileBackend(host.context, removalPending());

  if (!inputs.mounted) {
    _removalPending.store(false, std::memory_order_release);
    host.clearFailureNote(host.context);
    if (host.missingCard) host.missingCard(host.context, inputs.now);
    if (inputs.sleeping || inputs.fileManagerActive) {
      cancel();
      return;
    }
    if (!_maintenance.pending() &&
        static_cast<int32_t>(inputs.now - _nextBackgroundProbeMs) < 0)
      return;
    if (host.cardPresent && !host.cardPresent(host.context)) {
      cancel();
      return;
    }

    StorageMaintenance::Attempt reservation(_maintenance, inputs.now,
                                             inputs.legacyStorageBusy);
    if (!reservation.ready()) {
      if (reservation.status() == StorageMaintenance::Status::TimedOut)
        _nextBackgroundProbeMs = deadline(inputs.now, 30000);
      return;
    }
    _nextBackgroundProbeMs = deadline(inputs.now, 30000);
    if (host.backgroundMount(host.context)) emit(host, Event::Remounted, inputs.now);
    return;
  }

  if (host.readCardState && !_removalPending.load(std::memory_order_acquire) &&
      static_cast<int32_t>(inputs.now - _nextDetectMs) >= 0) {
    _nextDetectMs = deadline(inputs.now, 500);
    const CardState state = host.readCardState(host.context);
    if (state != CardState::Absent) {
      _absentSamples = 0;
      _absentFirstMs = 0;
    } else if (!_absentSamples ||
               static_cast<uint32_t>(inputs.now - _absentFirstMs) > 1100) {
      _absentSamples = 1;
      _absentFirstMs = inputs.now;
    } else if (++_absentSamples >= 2) {
      _absentSamples = 0;
      _absentFirstMs = 0;
      _removalPending.store(true, std::memory_order_release);
      host.noteFailure(host.context);
    }
  }

  if (removalPending()) {
    StorageMaintenance::Attempt reservation(_maintenance, inputs.now,
                                             inputs.legacyStorageBusy);
    if (!reservation.ready()) return;
    if (!host.unmount(host.context)) return;
    _removalPending.store(false, std::memory_order_release);
    emit(host, Event::Removed, inputs.now);
    return;
  }

  if (!_maintenance.pending()) {
    if (inputs.failureNoted) {
      if (_nextProbeMs && static_cast<int32_t>(inputs.now - _nextProbeMs) < 0)
        return;
    } else {
      if (inputs.sleeping) return;
      if (static_cast<int32_t>(inputs.now - _nextBackgroundProbeMs) < 0) return;
    }
  }

  StorageMaintenance::Attempt reservation(_maintenance, inputs.now,
                                           inputs.legacyStorageBusy);
  if (!reservation.ready()) {
    if (reservation.status() == StorageMaintenance::Status::TimedOut) {
      _nextProbeMs = deadline(inputs.now, 5000);
      _nextBackgroundProbeMs = deadline(inputs.now, 30000);
    }
    return;
  }
  _nextProbeMs = deadline(inputs.now, 5000);
  _nextBackgroundProbeMs = deadline(inputs.now, 30000);
  host.clearFailureNote(host.context);
  if (host.probeAlive(host.context)) {
    emit(host, Event::HealthyProbe, inputs.now);
    return;
  }
  if (!host.unmount(host.context)) return;
  if (host.mount(host.context))
    emit(host, Event::Remounted, inputs.now);
  else
    emit(host, Event::Lost, inputs.now);
}

} } // namespace ui::services
