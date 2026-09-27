// SPDX-License-Identifier: GPL-3.0-or-later
#include "SdRestoreJob.h"
#include "../platform/StorageAccess.h"

namespace ui {
namespace services {
namespace {

class WdtScope {
public:
  WdtScope(void *context, void (*begin)(void *), void (*end)(void *))
      : _context(context), _end(end) { begin(context); }
  ~WdtScope() { _end(_context); }
  WdtScope(const WdtScope &) = delete;
  WdtScope &operator=(const WdtScope &) = delete;
private:
  void *_context;
  void (*_end)(void *);
};

struct MigrationContext {
  SdRestoreJob *job;
  platform::StorageTransition *transition;
};

} // namespace

bool SdRestoreJob::request() {
  if (_pending) return false;
  _pending = true;
  return true;
}

bool SdRestoreJob::validHost() const {
  return !(_policy.verifyProfile && _policy.forceCopy) &&
         _host.lifecycleBusy && _host.mountSd && _host.heavyWdtBegin && _host.heavyWdtEnd &&
         _host.persistHistory && _host.flushDiscovered && _host.flushContacts &&
         _host.persistSyncHistory && _host.flushPrefs &&
         (!_policy.verifyProfile || _host.profileMatches) &&
         _host.prepareMigration && _host.armLatch &&
         _host.guardedMigration && _host.migrate &&
         _host.markBlocked && _host.clearLatch && _host.setUseSd &&
         _host.clockFloor && _host.setClockFloor && _host.delayMs && _host.reboot;
}

SdRestoreJob::Status SdRestoreJob::tick() {
  if (!_pending) return Status::Idle;
  if (!validHost()) {
    _pending = false;
    return Status::InvalidHost;
  }
  if (platform::storageAccess().admissionClosed()) return Status::Deferred;
  if (_host.lifecycleBusy(_host.context)) return Status::Deferred;
  _pending = false;
  if (!_host.mountSd(_host.context)) return Status::NoCard;

  WdtScope heavy(_host.context, _host.heavyWdtBegin, _host.heavyWdtEnd);
  // Drain RAM-backed stores and the queued A/B preferences before opening
  // either filesystem for the migration walk.
  _host.persistHistory(_host.context);
  _host.flushDiscovered(_host.context);
  _host.flushContacts(_host.context);
  _host.persistSyncHistory(_host.context);
  if (!_host.flushPrefs(_host.context)) {
    _host.markBlocked(_host.context);
    return Status::InternalBusy;
  }
  // A tile, notification or history consumer can have started while drains ran.
  // Keep the request queued; the coordinator will retry after the next LVGL pass.
  if (_host.lifecycleBusy(_host.context)) {
    _pending = true;
    return Status::Deferred;
  }
  if (platform::storageAccess().admissionClosed()) {
    _pending = true;
    return Status::Deferred;
  }
  // Close admission only after the pre-copy drains. A concurrent reader may
  // have arrived after lifecycleBusy(); leave the request queued if so.
  platform::StorageTransition transition;
  if (!transition.requested() || !transition.enter()) {
    _pending = true;
    return Status::Deferred;
  }
  if (_policy.verifyProfile && !_host.profileMatches(_host.context))
    return Status::ProfileMismatch;
  if (!_host.prepareMigration(_host.context)) {
    _host.markBlocked(_host.context);
    return Status::PrepareFailed;
  }
  if (!_host.armLatch(_host.context)) {
    _host.markBlocked(_host.context);
    return Status::LatchFailed;
  }
  MigrationContext context{this, &transition};
  return _host.guardedMigration(_host.context, runMigrationThunk, &context);
}

SdRestoreJob::Status SdRestoreJob::runMigrationThunk(void *context) {
  auto &migration = *static_cast<MigrationContext *>(context);
  return migration.job->runMigration(*migration.transition);
}

SdRestoreJob::Status SdRestoreJob::runMigration(platform::StorageTransition &transition) {
  if (!_host.migrate(_host.context, _policy.forceCopy)) {
    // The durable latch remains armed, so a later boot cannot adopt a partial
    // SD tree. Do not touch that card again during this attempt.
    _host.markBlocked(_host.context);
    return Status::CopyFailed;
  }
  _host.clearLatch(_host.context);
  _host.setUseSd(_host.context);
  _host.setClockFloor(_host.context, _host.clockFloor(_host.context));
  // The final preferences flush uses an asynchronous writer. Reopen admission
  // after the copy and state switch, while retaining both WDT guards below.
  transition.release();
  _host.flushPrefs(_host.context);
  _host.delayMs(_host.context, 400);
  _host.reboot(_host.context);
  return Status::RebootRequested; // Reached only by fake reboot callbacks.
}

} // namespace services
} // namespace ui
