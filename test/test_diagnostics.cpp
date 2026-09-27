// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/SystemDiagnostics.h"
#include "ui-touch/services/StorageUsage.h"
#include "ui-touch/platform/StorageAccess.h"
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
using namespace ui;
StorageUsage::Snapshot capacity(void *) {
  StorageUsage::Snapshot result;
  result.ok = true;
  result.total = (uint64_t(8) << 32) + 123;
  result.free = (uint64_t(4) << 32) + 456;
  return result;
}
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false, release = false;
};
StorageUsage::Snapshot blocking(void *context) {
  auto &gate = *static_cast<Gate *>(context);
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.entered = true;
  gate.condition.notify_all();
  gate.condition.wait(lock, [&] { return gate.release; });
  return capacity(nullptr);
}
} // namespace
void diagnosticsRegression() {
  using namespace ui::diagnostics;
  StallHistory history;
  Stall entries[16];
  assert(history.newest(entries, 16) == 0);
  history.record(0, "short", 199);
  assert(history.newest(entries, 16) == 0);
  char tag[] = "owned";
  history.record(1, tag, 200);
  tag[0] = 'X';
  assert(history.newest(entries, 1) == 1 && !strcmp(entries[0].tag, "owned"));
  for (unsigned i = 2; i < 22; ++i)
    history.record(i, "loop", i == 21 ? 70000 : 300);
  assert(history.newest(entries, 16) == 16 && entries[0].atSeconds == 21 && entries[15].atSeconds == 6);
  assert(entries[0].durationMs == 65535 && !history.newest(nullptr, 16));
  Snapshot data;
  data.hardware.available = true;
  data.hardware.uptimeSeconds = 90061;
  data.hardware.dram.total = 4096;
  data.hardware.dram.free = 512;
  data.hardware.psram.total = 8 * 1024 * 1024;
  data.hardware.nvsTotal = 100;
  data.hardware.nvsUsed = 75;
  data.hardware.internalTotal = 1024 * 1024;
  data.hardware.internalUsed = 512 * 1024;
  strcpy(data.hardware.chip, "ESP32-S3");
  strcpy(data.hardware.reset, "Brownout");
  data.rxEvents = 8;
  data.rxRead = UINT32_MAX;
  data.rxErrors = 10; // no overflow in late-lost
  data.historyReady = true;
  strcpy(data.historySave, "FAIL x2 (12:30)\n  append failed");
  data.sdAvailable = data.sdChecked = data.sdMounted = data.sdCapacitySupported = true;
  data.sdAttempts = 2;
  data.sdHz = 4000000;
  data.nowSeconds = 25;
  data.stallCount = history.newest(data.stalls, 6);
  strcpy(data.firmware, "beta_100");
  strcpy(data.buildDate, "2026-09-21");
  data.forkBuild = true;
  char text[2048];
  formatLive(data, text, sizeof text);
  assert(strstr(text, "1d 01h 01m 01s") && strstr(text, "late-lost: 0") && strstr(text, "append failed"));
  formatRest(data, text, sizeof text);
  assert(strstr(text, "size: checking...") && strstr(text, "75%") && strstr(text, "beta_100") &&
         strstr(text, "-4s  loop  65535ms"));
  data.sdCapacityDone = true;
  data.sdCapacityOk = false;
  formatRest(data, text, sizeof text);
  assert(strstr(text, "capacity read failed"));
  data.sdCapacityOk = true;
  data.sdTotal = uint64_t(32) << 30;
  data.sdFree = uint64_t(16) << 30;
  formatRest(data, text, sizeof text);
  assert(strstr(text, "size: 32768 MB") && strstr(text, "free: 16384 MB"));
  data.sdMounted = false;
  data.sdBeginOk = false;
  formatRest(data, text, sizeof text);
  assert(strstr(text, "last init: failed"));
  data.sdAvailable = false;
  formatRest(data, text, sizeof text);
  assert(strstr(text, "not available on this hardware"));
  formatMemory(data.hardware, text, sizeof text);
  assert(strstr(text, "used  4 / 4 KB"));
  for (size_t size : {size_t(0), size_t(1), size_t(2), size_t(32), size_t(1024), size_t(2048)}) {
    for (int format = 0; format < 3; ++format) {
      unsigned char guarded[2050];
      memset(guarded, 0xa5, sizeof guarded);
      auto *out = reinterpret_cast<char *>(guarded + 1);
      if (format == 0)
        formatLive(data, out, size);
      else if (format == 1)
        formatRest(data, out, size);
      else
        formatMemory(data.hardware, out, size);
      assert(guarded[0] == 0xa5 && guarded[size + 1] == 0xa5);
      if (size)
        assert(memchr(out, 0, size));
    }
  }
  formatLive(data, nullptr, 100);
  data.hardware.available = false;
  strcpy(text, "previous");
  formatLive(data, text, sizeof text);
  assert(!text[0]);
  StorageUsage usage;
  const auto ensure = []() -> bool { return true; };
  assert(!usage.refresh(0, []() -> bool { return false; }) && !usage.busy());
  assert(!usage.refresh(29999, ensure));
  assert(usage.refresh(30000, ensure) && usage.busy());
  Gate gate;
  std::thread worker([&] { assert(usage.run(&gate, blocking)); });
  {
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.condition.wait(lock, [&] { return gate.entered; });
  }
  usage.poll();
  assert(!usage.snapshot().done && usage.busy() && !usage.refresh(60000, ensure));
  usage.invalidate(); // a replaced mount cannot publish this worker's result
  assert(usage.busy() && !usage.snapshot().done);
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.release = true;
  }
  gate.condition.notify_all();
  worker.join();
  assert(!usage.busy());
  usage.poll();
  assert(!usage.snapshot().done);
  assert(usage.refresh(UINT32_MAX - 10000, ensure) && usage.run(nullptr, capacity));
  usage.poll();
  assert(usage.snapshot().done && usage.snapshot().ok && usage.snapshot().total == capacity(nullptr).total &&
         usage.snapshot().free == capacity(nullptr).free);
  assert(!usage.refresh(10000, ensure) && usage.refresh(20000, ensure));
  usage.invalidate();
  usage.poll();
  assert(!usage.busy() && !usage.run(nullptr, capacity)); // cancel wins claim
  assert(usage.refresh(20001, ensure) && usage.run(nullptr, [](void *) {
    auto result = capacity(nullptr);
    result.free = result.total + 1;
    return result;
  }));
  usage.poll();
  assert(usage.snapshot().done && !usage.snapshot().ok && !usage.snapshot().total);

  usage.invalidate();
  usage.poll();
  assert(usage.refresh(50000, ensure) && usage.run(nullptr, capacity));
  const auto &published = usage.snapshot();
  const auto oldTotal = published.total;
  std::thread notifier([&] { usage.invalidate(); });
  notifier.join();
  assert(published.total == oldTotal && published.done); // notifier cannot mutate UI memory
  usage.poll();
  assert(!published.done && !published.total);
  assert(usage.refresh(50001, ensure));
  usage.invalidate();
  bool readCalled = false;
  assert(usage.run(&readCalled, [](void *context) {
    *static_cast<bool *>(context) = true;
    return capacity(nullptr);
  }));
  assert(!readCalled);
  usage.poll();
  assert(!usage.snapshot().done);
  StorageUsage deferred;
  int reads = 0;
  assert(deferred.refresh(1, ensure));
  {
    platform::StorageTransition transition;
    assert(transition.requested() && transition.ready());
    bool ran = true;
    std::thread denied([&] {
      ran = deferred.run(&reads, [](void *context) {
        ++*static_cast<int *>(context);
        return capacity(nullptr);
      });
    });
    denied.join();
    assert(!ran && !reads && deferred.busy() && !deferred.snapshot().done);
  }
  assert(deferred.run(&reads, [](void *context) {
    ++*static_cast<int *>(context);
    return capacity(nullptr);
  }));
  deferred.poll();
  assert(reads == 1 && deferred.snapshot().done && deferred.snapshot().ok);
  assert(platform::storageAccess().readerCount() == 0);
  std::puts("Diagnostics: bounded formatting, owned stall history, coherent SD snapshots, mount invalidation "
            "and scheduling passed.");
}
