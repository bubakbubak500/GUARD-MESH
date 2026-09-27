// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StorageMaintenance.h"
#include <atomic>
#include <cstdint>

namespace ui { namespace services {

class SdHealthMonitor {
 public:
  enum class CardState : uint8_t { Present, Absent, Unknown };
  enum class Event : uint8_t { Adopted, Remounted, Lost, Removed, HealthyProbe };

  struct Inputs {
    uint32_t now = 0;
    bool mounted = false;
    bool sleeping = false;
    bool fileManagerActive = false;
    bool failureNoted = false;
    bool legacyStorageBusy = false;
    bool formatPending = false;
    bool adoptionAllowed = false;
  };

  struct Host {
    void *context = nullptr;
    bool (*adoptLiveMount)(void *) = nullptr;
    bool (*cardPresent)(void *) = nullptr;
    CardState (*readCardState)(void *) = nullptr;
    bool (*probeAlive)(void *) = nullptr;
    bool (*unmount)(void *) = nullptr;
    bool (*mount)(void *) = nullptr;
    bool (*backgroundMount)(void *) = nullptr;
    void (*clearFailureNote)(void *) = nullptr;
    void (*noteFailure)(void *) = nullptr;
    void (*reconcileBackend)(void *, bool) = nullptr;
    void (*missingCard)(void *, uint32_t) = nullptr;
    void (*event)(void *, Event, uint32_t) = nullptr;
  };

  SdHealthMonitor() = default;
  SdHealthMonitor(const SdHealthMonitor&) = delete;
  SdHealthMonitor& operator=(const SdHealthMonitor&) = delete;

  void tick(const Inputs& inputs, const Host& host);
  void cancel();
  bool removalPending() const { return _removalPending.load(std::memory_order_acquire); }

 private:
  bool validHost(const Host& host) const;
  static uint32_t deadline(uint32_t now, uint32_t interval);
  void emit(const Host& host, Event event, uint32_t now);

  StorageMaintenance _maintenance;
  uint32_t _nextProbeMs = 0;
  uint32_t _nextBackgroundProbeMs = 0;
  uint32_t _nextDetectMs = 0;
  uint32_t _absentFirstMs = 0;
  uint8_t _absentSamples = 0;
  std::atomic<bool> _removalPending{false};
};

} } // namespace ui::services
