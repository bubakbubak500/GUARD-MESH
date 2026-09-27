// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../platform/StorageAccess.h"
#include <cstdint>

namespace ui { namespace services {

// Keeps a lifecycle reservation across event-loop ticks while readers drain.
// Only a Ready Attempt enters the exclusive phase, and its destructor leaves
// that phase before control returns to the event loop.
class StorageMaintenance {
 public:
  enum class Status : uint8_t { Deferred, Ready, TimedOut };

  class Attempt {
   public:
    Attempt(StorageMaintenance& maintenance, uint32_t now, bool legacyBusy,
            uint32_t timeoutMs = 15000);
    ~Attempt();
    Attempt(const Attempt&) = delete;
    Attempt& operator=(const Attempt&) = delete;

    Status status() const { return _status; }
    bool ready() const { return _status == Status::Ready; }

   private:
    StorageMaintenance& _maintenance;
    Status _status = Status::Deferred;
    bool _ownsActive = false;
  };

  StorageMaintenance() = default;
  StorageMaintenance(const StorageMaintenance&) = delete;
  StorageMaintenance& operator=(const StorageMaintenance&) = delete;

  void cancel();
  bool pending() const { return _started && !_active; }

 private:
  friend class Attempt;
  void finishActive();

  platform::StorageTransition _transition{false};
  uint32_t _startedAt = 0;
  uintptr_t _context = 0;
  bool _started = false;
  bool _active = false;
};

} } // namespace ui::services
