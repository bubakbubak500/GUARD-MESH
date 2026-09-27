// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ui {
namespace platform { class StorageTransition; }
namespace services {

// Manual internal-to-SD recovery, run by the UI loop after LVGL has returned.
// The boot migration and SD mount lifecycle remain with their existing owners.
class SdRestoreJob {
public:
  enum class Status {
    Idle, Deferred, NoCard, InternalBusy, ProfileMismatch, PrepareFailed,
    LatchFailed, CopyFailed, RebootRequested, InvalidHost
  };
  struct Policy {
    const bool verifyProfile;
    const bool forceCopy;
    Policy(bool verifyProfile, bool forceCopy)
        : verifyProfile(verifyProfile), forceCopy(forceCopy) {}
  };
  struct Host {
    void *context = nullptr;
    bool (*lifecycleBusy)(void *) = nullptr;
    bool (*mountSd)(void *) = nullptr;
    void (*heavyWdtBegin)(void *) = nullptr;
    void (*heavyWdtEnd)(void *) = nullptr;
    void (*persistHistory)(void *) = nullptr;
    void (*flushDiscovered)(void *) = nullptr;
    void (*flushContacts)(void *) = nullptr;
    void (*persistSyncHistory)(void *) = nullptr;
    bool (*flushPrefs)(void *) = nullptr;
    bool (*profileMatches)(void *) = nullptr;
    bool (*prepareMigration)(void *) = nullptr;
    bool (*armLatch)(void *) = nullptr;
    // Platform creates a real scoped LoopWdtGuard around run(context).
    Status (*guardedMigration)(void *, Status (*run)(void *), void *runContext) = nullptr;
    bool (*migrate)(void *, bool forceCopy) = nullptr;
    void (*markBlocked)(void *) = nullptr;
    void (*clearLatch)(void *) = nullptr;
    void (*setUseSd)(void *) = nullptr;
    uint32_t (*clockFloor)(void *) = nullptr;
    void (*setClockFloor)(void *, uint32_t) = nullptr;
    void (*delayMs)(void *, unsigned) = nullptr;
    void (*reboot)(void *) = nullptr;
  };

  SdRestoreJob(Host host, Policy policy) : _host(host), _policy(policy) {}
  SdRestoreJob(const SdRestoreJob &) = delete;
  SdRestoreJob &operator=(const SdRestoreJob &) = delete;

  // A duplicate confirmation does not replace a queued request or its notice.
  bool request();
  bool pending() const { return _pending; }
  Status tick();

private:
  bool validHost() const;
  static Status runMigrationThunk(void *context);
  Status runMigration(platform::StorageTransition &transition);
  Host _host;
  const Policy _policy;
  bool _pending = false;
};

} // namespace services
} // namespace ui
