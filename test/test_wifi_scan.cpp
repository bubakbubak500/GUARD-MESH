// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/WifiScanJob.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
namespace {
using Job = ui::WifiScanJob;
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false, release = false;
};
void blocking(void *context, Job::Snapshot &result) {
  assert(result.add("new-network"));
  auto &gate = *static_cast<Gate *>(context);
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.entered = true;
  gate.condition.notify_all();
  gate.condition.wait(lock, [&] { return gate.release; });
  assert(result.add("second-network"));
  result.available = true;
  result.kick = -1;
  result.status = 2;
  result.duration = UINT32_MAX - 11;
  result.radioStatus = 3;
}
} // namespace
void wifiScanRegression() {
  Job::Snapshot list;
  assert(!list.add(nullptr) && !list.add(""));
  char name[70];
  memset(name, 'a', sizeof name);
  name[sizeof name - 1] = 0;
  assert(list.add(name) && strlen(list.ssids[0]) == 31);
  assert(!list.add(name));
  name[32] = 'b';
  assert(!list.add(name)); // deduplicate the stored bounded identity
  for (int i = 1; i < Job::Capacity; ++i) {
    snprintf(name, sizeof name, "SSID %d", i);
    assert(list.add(name));
  }
  assert(list.count == Job::Capacity && !list.add("overflow"));
  Job job;
  assert(!job.active() && !job.cancel() && !job.poll());
  assert(job.request() && !job.request());
  assert(job.cancel() && !job.run(nullptr, nullptr) && !job.poll());
  assert(job.request());
  assert(job.run(nullptr, [](void *, Job::Snapshot &result) {
    result.available = true;
    result.add("old-network");
  }));
  assert(job.active() && !job.request() && !job.cancel());
  assert(job.snapshot().count == 0 && job.poll());
  const auto &visible = job.snapshot();
  assert(visible.available && visible.count == 1 && !strcmp(visible.ssids[0], "old-network"));

  Gate gate;
  assert(job.request());
  std::thread worker([&] { assert(job.run(&gate, blocking)); });
  {
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.condition.wait(lock, [&] { return gate.entered; });
  }
  assert(!job.cancel() && !job.request() && !job.poll());
  job.failQueued(); // cannot change a claimed scan
  assert(visible.count == 1 && !strcmp(visible.ssids[0], "old-network"));
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.release = true;
    gate.condition.notify_all();
  }
  worker.join();
  // Worker completion itself cannot mutate a reference held by the UI.
  assert(visible.count == 1 && !strcmp(visible.ssids[0], "old-network"));
  assert(job.poll() && !job.active() && !job.poll());
  assert(visible.count == 2 && !strcmp(visible.ssids[0], "new-network") &&
         !strcmp(visible.ssids[1], "second-network"));
  assert(visible.status == 2 && visible.duration == UINT32_MAX - 11 && visible.radioStatus == 3);
  assert(job.request());
  job.failQueued();
  assert(job.active() && job.poll() && !visible.available && visible.count == 0);

  // Timeout cancellation competes with the worker for one state transition.
  for (int i = 0; i < 200; ++i) {
    assert(job.request());
    std::atomic<bool> go{false};
    bool ran = false;
    std::thread contender([&] {
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      ran = job.run(nullptr, [](void *, Job::Snapshot &result) { result.add("raced"); });
    });
    go.store(true, std::memory_order_release);
    const bool cancelled = job.cancel();
    contender.join();
    assert(cancelled != ran);
    assert(job.poll() == ran && !job.active());
  }
  puts("Wi-Fi scan: bounded unique SSIDs, coherent publication, unavailable executor and worker/cancel race "
       "passed.");
}
