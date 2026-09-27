// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
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
  };
  struct InstallRequest {
    Destination destination;
    bool beta;
    int version;
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
  };
  bool requestCheck(bool beta, uint32_t generation);
  bool takeCheck(CheckResult &);
  bool checkActive() const;
  bool requestInstall(Destination, bool beta, int version);
  bool takeInstall(Destination, InstallResult &);
  bool installActive() const;
  bool storageBusy() const;            // queued/running SD job, not a completed result
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
// UI-thread scheduling and channel identity. Stale worker results are consumed
// but cannot overwrite a newer choice, including stable -> beta -> stable.
class ReleaseMonitor {
public:
  bool selectChannel(bool beta);
  bool tick(FirmwareUpdateJobs &, uint32_t now, bool networkReady, int currentVersion,
            bool (*ensureExecutor)());
  int latest() const { return _latest; }
  bool checked() const { return _checked; }

private:
  bool _beta = false, _checked = false, _scheduled = false, _force = true;
  int _latest = -1;
  uint8_t _failures = 0;
  uint32_t _generation = 0, _next = 0;
};
// Incremental bounded parser for the release listing; does not allocate a JSON tree.
class ReleaseListing {
public:
  void feed(char);
  int latest() const;
  static int version(const char *tag);

private:
  int _best = -1, _number = -1;
  unsigned _matched = 0;
  bool _digits = false, _overflow = false;
};
} // namespace ui
