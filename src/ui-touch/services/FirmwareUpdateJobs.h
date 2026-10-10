// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include "../models/FirmwareRelease.h"
namespace ui {
// One UI producer/consumer, one executor. Neither side shares mutable buffers.
// Stop the executor before destroying this owner. All callbacks are synchronous.
class FirmwareUpdateJobs {
public:
  enum class Destination { Ota, Sd };
  struct CheckRequest {
    bool beta;
    uint32_t generation;
  };
  struct CheckResult {
    CheckRequest request;
    int latest;
    FirmwareRelease release;
    bool ok;
    char message[80];
  };
  struct InstallRequest {
    Destination destination;
    bool beta;
    int version;
    FirmwareRelease release;
  };
  struct InstallResult {
    InstallRequest request;
    bool ok;
    char message[80];
  };
  struct Progress {
    void *context;
    void (*report)(void *, int);
  };
  struct Backend {
    void *context;
    int (*check)(void *, bool beta);
    void (*install)(void *, const InstallRequest &, Progress, InstallResult &);
    void (*resolve)(void *, CheckResult &);
    Backend(void *c, int (*checkFn)(void *, bool),
            void (*installFn)(void *, const InstallRequest &, Progress, InstallResult &),
            void (*resolveFn)(void *, CheckResult &) = nullptr)
      : context(c), check(checkFn), install(installFn), resolve(resolveFn) {}
  };
  bool requestCheck(bool beta, uint32_t generation);
  bool takeCheck(CheckResult &);
  bool checkActive() const;
  bool requestInstall(Destination, bool beta, int version);
  bool requestInstall(const FirmwareRelease &);
  bool takeInstall(Destination, InstallResult &);
  bool installActive() const;
  bool storageBusy() const;            // queued/running install, not a completed result
  int installState(Destination) const; // 0 idle, 1 queued/running, 2 success, 3 failure
  int progress() const { return _progress.load(std::memory_order_relaxed); }
  void failQueuedCheck();
  void failQueuedInstall(const char *message);
  bool runCheck(const Backend &);
  bool runInstall(const Backend &);

private:
  enum State { Idle, Queued, Running, Ready };
  std::atomic<State> _checkState{Idle}, _installState{Idle};
  std::atomic<int> _progress{0};
  CheckResult _check{};
  InstallResult _install{};
};
} // namespace ui
