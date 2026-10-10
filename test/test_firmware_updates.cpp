// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/FirmwareUpdateJobs.h"
#include "ui-touch/platform/StorageAccess.h"
#include "ui-touch/platform/esp32/FirmwareUpdateTransport.h"
#include "ui-touch/services/FirmwareUpdateSource.h"
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
void blockedTransport() {
  assert(!strcmp(ui::firmwareUpdate::releasesUrl,
                 "https://github.com/bubakbubak500/GUARD-MESH/releases"));
  Jobs jobs;
  // Null socket handles prove these entry points cannot touch a network client.
  Jobs::CheckResult check;
  assert(jobs.requestCheck(true, 9));
  assert(ui::platform::runFirmwareCheck(jobs, nullptr, nullptr));
  assert(jobs.takeCheck(check) && check.latest == -1 && check.request.generation == 9);
  for (auto destination : {Destination::Ota, Destination::Sd}) {
    assert(jobs.requestInstall(destination, true, 999));
    assert(ui::platform::runFirmwareInstall(jobs, nullptr, nullptr));
    Jobs::InstallResult result;
    assert(jobs.takeInstall(destination, result) && !result.ok && jobs.progress() == 0);
    assert(strstr(result.message, "GUARD-MESH GitHub releases"));
  }
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
  transactions();
  blockedTransport();
  storageAdmission();
}
