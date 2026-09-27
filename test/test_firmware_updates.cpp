// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/FirmwareUpdateJobs.h"
#include "ui-touch/platform/StorageAccess.h"
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <thread>
namespace {
using Jobs = ui::FirmwareUpdateJobs;
using Destination = Jobs::Destination;
int executorStarts;
bool executorOk;
bool ensureExecutor() {
  ++executorStarts;
  return executorOk;
}
struct Backend {
  Jobs *jobs;
  std::atomic<bool> entered{false}, finish{false};
  bool blocking = false, success = true, sawBeta = false;
  int latest = 27, calls = 0;
  Jobs::InstallRequest request{};
  Jobs::Backend api() {
    return {this,
            [](void *context, bool beta) {
              auto &self = *static_cast<Backend *>(context);
              ++self.calls;
              self.sawBeta = beta;
              self.entered.store(true, std::memory_order_release);
              while (self.blocking && !self.finish.load(std::memory_order_acquire))
                std::this_thread::yield();
              return self.latest;
            },
            [](void *context, const Jobs::InstallRequest &request, Jobs::Progress progress,
               Jobs::InstallResult &result) {
              auto &self = *static_cast<Backend *>(context);
              ++self.calls;
              self.request = request;
              progress.report(progress.context, -2);
              assert(self.jobs->progress() == 0);
              progress.report(progress.context, 123);
              assert(self.jobs->progress() == 100);
              progress.report(progress.context, 42);
              self.entered.store(true, std::memory_order_release);
              while (self.blocking && !self.finish.load(std::memory_order_acquire))
                std::this_thread::yield();
              result.ok = self.success;
              snprintf(result.message, sizeof result.message, "%s",
                       self.success ? "/BINS/test.bin" : "short read");
              // The owner must never trust backend-written request fields.
              result.request.version = 999;
            }};
  }
};
void listing() {
  assert(ui::ReleaseListing::version("beta_123") == 123);
  assert(ui::ReleaseListing::version("beta_2147483647") == INT_MAX);
  for (auto *tag : {"", "dev", "beta_", "beta_-1", "beta_12bad", "beta_2147483648"})
    assert(ui::ReleaseListing::version(tag) == -1);
  ui::ReleaseListing listing;
  const char *text =
      "[\"beta_9\",\"beta_100\",\"beta_23\",\"beta_99999999999999999999\",\"bbeta_41\"]beta_101";
  for (const char *p = text; *p; ++p)
    listing.feed(*p);
  assert(listing.latest() == 101);
  ui::ReleaseListing bad;
  for (const char *p = "beta_99999999999999999999"; *p; ++p)
    bad.feed(*p);
  assert(bad.latest() == -1);
}
void transactions() {
  Jobs jobs;
  Backend backend;
  backend.jobs = &jobs;
  Jobs::CheckResult check;
  Jobs::InstallResult result;
  assert(!jobs.takeCheck(check) && !jobs.runCheck(backend.api()));
  assert(!jobs.requestInstall(Destination::Ota, false, -1));
  assert(jobs.requestInstall(Destination::Sd, true, 15));
  assert(jobs.storageBusy() && jobs.installState(Destination::Sd) == 1 &&
         jobs.installState(Destination::Ota) == 0);
  assert(!jobs.requestInstall(Destination::Ota, false, 16));
  backend.blocking = true;
  std::thread worker([&] { assert(jobs.runInstall(backend.api())); });
  while (!backend.entered.load(std::memory_order_acquire))
    std::this_thread::yield();
  assert(jobs.storageBusy() && jobs.progress() == 42);
  assert(!jobs.takeInstall(Destination::Sd, result));
  jobs.failQueuedInstall("must not overwrite running job");
  assert(jobs.installState(Destination::Sd) == 1);
  backend.finish.store(true, std::memory_order_release);
  worker.join();
  assert(!jobs.storageBusy() && jobs.installActive() && jobs.installState(Destination::Sd) == 2);
  assert(!jobs.takeInstall(Destination::Ota, result));
  assert(jobs.takeInstall(Destination::Sd, result) && result.ok && result.request.version == 15 &&
         result.request.beta);
  assert(!strcmp(result.message, "/BINS/test.bin") && jobs.progress() == 100 && !jobs.installActive());
  assert(backend.request.version == 15 && backend.request.beta &&
         backend.request.destination == Destination::Sd);
  assert(jobs.requestInstall(Destination::Ota, false, 17));
  jobs.failQueuedInstall("executor unavailable");
  assert(!jobs.runInstall(backend.api()) && jobs.takeInstall(Destination::Ota, result) && !result.ok);
  assert(!strcmp(result.message, "executor unavailable"));
  backend.blocking = false;
  backend.success = false;
  assert(jobs.requestInstall(Destination::Sd, false, 18) && jobs.runInstall(backend.api()));
  assert(jobs.takeInstall(Destination::Sd, result) && !result.ok && !strcmp(result.message, "short read"));
  assert(jobs.requestCheck(true, 123) && !jobs.requestCheck(false, 124));
  assert(jobs.runCheck(backend.api()) && jobs.takeCheck(check));
  assert(check.request.beta && check.request.generation == 123 && check.latest == 27 && backend.sawBeta);
}
void scheduling() {
  Jobs jobs;
  Backend backend;
  backend.jobs = &jobs;
  ui::ReleaseMonitor monitor;
  executorStarts = 0;
  executorOk = true;
  monitor.tick(jobs, 0, true, -1, ensureExecutor);
  assert(!jobs.checkActive());
  monitor.tick(jobs, 1, false, 20, ensureExecutor);
  assert(!jobs.checkActive());
  monitor.tick(jobs, 2, true, 20, ensureExecutor);
  assert(jobs.checkActive() && executorStarts == 1);
  // Even returning to the same channel must reject the former generation.
  assert(monitor.selectChannel(true) && monitor.selectChannel(false));
  assert(jobs.runCheck(backend.api()));
  assert(!monitor.tick(jobs, 3, true, 20, ensureExecutor));
  assert(!monitor.checked() && monitor.latest() == -1 && jobs.checkActive());
  assert(jobs.runCheck(backend.api()));
  assert(monitor.tick(jobs, 4, true, 20, ensureExecutor) && monitor.checked() && monitor.latest() == 27);
  monitor.tick(jobs, 4 + 21600000u - 1, true, 20, ensureExecutor);
  assert(!jobs.checkActive());
  monitor.tick(jobs, 4 + 21600000u, true, 20, ensureExecutor);
  assert(jobs.checkActive());
  jobs.failQueuedCheck();
  assert(monitor.tick(jobs, 22000000u, false, 20, ensureExecutor) && monitor.latest() == -1);
  monitor.tick(jobs, 22059999u, true, 20, ensureExecutor);
  assert(!jobs.checkActive());
  monitor.tick(jobs, 22060000u, true, 20, ensureExecutor);
  assert(jobs.checkActive());
  // A channel changed while a worker was running: only its immutable request is read.
  backend.blocking = true;
  backend.entered = false;
  backend.finish = false;
  std::thread worker([&] { assert(jobs.runCheck(backend.api())); });
  while (!backend.entered.load(std::memory_order_acquire))
    std::this_thread::yield();
  monitor.selectChannel(true);
  monitor.tick(jobs, 22060001u, true, 20, ensureExecutor);
  assert(!monitor.checked() && jobs.checkActive());
  backend.finish.store(true, std::memory_order_release);
  worker.join();
  assert(!monitor.tick(jobs, 22060002u, true, 20, ensureExecutor) && jobs.checkActive());
  backend.blocking = false;
  jobs.runCheck(backend.api());
  assert(monitor.tick(jobs, 22060003u, true, 20, ensureExecutor) && backend.sawBeta);
  Jobs wrapJobs;
  ui::ReleaseMonitor wrap;
  executorOk = false;
  const uint32_t start = UINT32_MAX - 1000u;
  wrap.tick(wrapJobs, start, true, 1, ensureExecutor);
  assert(wrap.tick(wrapJobs, start + 1, true, 1, ensureExecutor) && wrap.checked() && wrap.latest() == -1);
  wrap.tick(wrapJobs, start + 60000u, true, 1, ensureExecutor);
  assert(!wrapJobs.checkActive());
  wrap.tick(wrapJobs, start + 60001u, true, 1, ensureExecutor);
  assert(wrapJobs.checkActive());
}
void storageAdmission() {
  Jobs jobs;
  Backend backend;
  backend.jobs = &jobs;
  assert(jobs.requestInstall(Destination::Sd, true, 41));
  {
    ui::platform::StorageTransition transition;
    assert(transition.requested() && transition.ready());
    bool ran = true;
    std::thread denied([&] { ran = jobs.runInstall(backend.api()); });
    denied.join();
    assert(!ran && backend.calls == 0 && jobs.storageBusy() && jobs.progress() == 0 &&
           jobs.installState(Destination::Sd) == 1);
  }
  backend.success = false;
  assert(jobs.runInstall(backend.api()));
  Jobs::InstallResult result;
  assert(backend.calls == 1 && jobs.takeInstall(Destination::Sd, result) && !result.ok &&
         result.request.version == 41 && result.request.beta);
  assert(ui::platform::storageAccess().readerCount() == 0);
}
} // namespace
void firmwareUpdatesRegression() {
  listing();
  transactions();
  scheduling();
  storageAdmission();
}
