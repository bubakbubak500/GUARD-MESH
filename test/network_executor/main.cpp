// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic fake FreeRTOS runtime for the actual ESP32 executor source.
#include "ui-touch/platform/esp32/SharedNetworkExecutor.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Executor = ui::platform::SharedNetworkExecutor;
using Key = Executor::TileKey;

namespace fake {
struct Stop {};
struct Queue {
  unsigned capacity = 0;
  std::size_t itemSize = 0;
  std::deque<std::vector<uint8_t> > items;
};
Queue queue;
TaskFunction_t task = nullptr;
void *taskParameter = nullptr;
int taskHandle = 1;
bool locked = false;
bool failQueue = false;
bool failStack = false;
bool failTask = false;
bool failSend = false;
bool completeInSend = false;
bool leaseActive = false;
bool httpClosed = false;
uint16_t pendingInTile = 0;
std::vector<std::size_t> stackAttempts;
std::string events;

void event(const char *name) {
  if (!events.empty()) events += ',';
  events += name;
}
void enter(portMUX_TYPE *mux) {
  assert(mux && !locked && !mux->locked);
  locked = mux->locked = true;
}
void leave(portMUX_TYPE *mux) {
  assert(mux && locked && mux->locked);
  locked = mux->locked = false;
}
void unlocked() { assert(!locked); }
void pump() {
  assert(task != nullptr);
  try { task(taskParameter); }
  catch (const Stop &) { assert(!locked); }
}
} // namespace fake

void *heap_caps_malloc(std::size_t bytes, unsigned caps) {
  fake::unlocked();
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  fake::stackAttempts.push_back(bytes);
  return fake::failStack ? nullptr : std::malloc(bytes);
}
QueueHandle_t xQueueCreate(unsigned length, std::size_t itemSize) {
  fake::unlocked();
  assert(length == ui::TileRequestLedger::QueueCapacity && itemSize == sizeof(Executor::TileWork));
  if (fake::failQueue) return nullptr;
  fake::queue.capacity = length;
  fake::queue.itemSize = itemSize;
  return &fake::queue;
}
int xQueueSend(QueueHandle_t handle, const void *item, unsigned waitTicks) {
  fake::unlocked();
  assert(handle == &fake::queue && item && waitTicks == 0);
  if (fake::failSend || fake::queue.items.size() >= fake::queue.capacity) return pdFALSE;
  const uint8_t *bytes = static_cast<const uint8_t *>(item);
  fake::queue.items.push_back(std::vector<uint8_t>(bytes, bytes + fake::queue.itemSize));
  if (fake::completeInSend) fake::pump();
  return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle, void *item, unsigned waitTicks) {
  fake::unlocked();
  assert(handle == &fake::queue && item && waitTicks == 250);
  fake::event("R250");
  if (fake::queue.items.empty()) throw fake::Stop{};
  std::memcpy(item, fake::queue.items.front().data(), fake::queue.itemSize);
  fake::queue.items.pop_front();
  return pdTRUE;
}
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name,
                                           unsigned stackDepth, void *parameter,
                                           unsigned priority, StackType_t *stack,
                                           StaticTask_t *tcb, int core) {
  fake::unlocked();
  assert(fn && name && std::strcmp(name, "tile_fetch") == 0);
  assert(stackDepth >= 6 * 1024 / sizeof(StackType_t));
  assert(priority == 1 && stack && tcb && core == 0);
  if (fake::failTask) return nullptr;
  fake::task = fn;
  fake::taskParameter = parameter;
  return &fake::taskHandle;
}
void vTaskDelay(unsigned ticks) {
  fake::unlocked();
  if (ticks == 1) {
    fake::event("D1");
    return;
  }
  assert(ticks == 7 && !fake::leaseActive && fake::httpClosed &&
         fake::pendingInTile > 0 && Executor::pending() == fake::pendingInTile - 1);
  fake::event("D7");
}

namespace {
struct Fixture {
  int prioritiesRemaining = 0;
  int tiles = 0;
  int deferrals = 0;
  uint32_t deferredToken = 0;
  Executor::Outcome outcome = Executor::Outcome::Success;
  static bool priority(void *context, void *client, void *http) {
    Fixture &f = *static_cast<Fixture *>(context);
    assert(client && http);
    fake::event("P");
    if (f.prioritiesRemaining) { --f.prioritiesRemaining; return true; }
    return false;
  }
  static Executor::Completion tile(void *context, const Executor::TileWork &work,
                                   void *client, void *http) {
    Fixture &f = *static_cast<Fixture *>(context);
    auto &request = *static_cast<HTTPClient *>(http);
    assert(client && work.token != 0 && request.connectTimeout == 8000 &&
           request.timeout == 15000 && !request.reuse);
    assert(!fake::locked && !fake::leaseActive);
    if (f.deferredToken) assert(work.token == f.deferredToken);
    if (f.deferrals) {
      --f.deferrals;
      f.deferredToken = work.token;
      f.prioritiesRemaining = 1;
      assert(Executor::pending() == 1 && Executor::seen(work.key));
      assert(!Executor::enqueueTile(work.key));
      fake::event("W");
      return Executor::Completion(Executor::Outcome::Deferred, 1);
    }
    f.deferredToken = 0;
    fake::pendingInTile = Executor::pending();
    fake::leaseActive = true;
    fake::event("T");
    ++f.tiles;
    request.end();
    fake::httpClosed = request.ended;
    fake::leaseActive = false;
    return Executor::Completion(f.outcome, 7);
  }
  Executor::Host host() {
    Executor::Host h;
    h.context = this;
    h.runPriority = priority;
    h.runTile = tile;
    return h;
  }
};

void queueCreateFailure() {
  Fixture f;
  fake::failQueue = true;
  Executor::configure(f.host());
  assert(!Executor::enqueueTile(Key(1, 2, 3)));
  assert(!Executor::started() && Executor::pending() == 0 && fake::stackAttempts.empty());
}
void stackFailure() {
  Fixture f;
  fake::failStack = true;
  Executor::configure(f.host());
  assert(!Executor::enqueueTile(Key(1, 2, 3)));
  assert((fake::stackAttempts == std::vector<std::size_t>{8192, 7168, 6144}));
  assert(!Executor::started() && Executor::pending() == 0 &&
         !Executor::diagnostics().spawnOk);
}
void taskFailureRetry() {
  Fixture f;
  fake::failTask = true;
  Executor::configure(f.host());
  assert(!Executor::enqueueTile(Key(1, 2, 3)));
  assert(!Executor::started() && Executor::pending() == 0 &&
         !Executor::seen(Key(1, 2, 3)) && !Executor::diagnostics().spawnOk);
  fake::failTask = false;
  assert(Executor::enqueueTile(Key(1, 2, 3)));
  assert(Executor::started() && Executor::pending() == 1 && Executor::diagnostics().spawnOk);
  fake::pump();
  assert(f.tiles == 1 && Executor::pending() == 0 && Executor::seen(Key(1, 2, 3)));
}
void fullQueueRollback() {
  Fixture f;
  Executor::configure(f.host());
  assert(Executor::ensureRunning());
  for (int x = 0; x < 64; ++x)
    assert(Executor::enqueueTile(Key(1, x, 3)));
  assert(fake::queue.items.size() == 64 && Executor::pending() == 64);
  const Key rejected(1, 64, 3);
  assert(!Executor::enqueueTile(rejected));
  assert(Executor::pending() == 64 && !Executor::seen(rejected));
  fake::pump();
  assert(f.tiles == 64 && Executor::pending() == 0);
  assert(Executor::enqueueTile(rejected));
  fake::pump();
  assert(f.tiles == 65 && Executor::pending() == 0);
}
void immediateCompletion() {
  Fixture f;
  f.outcome = Executor::Outcome::RetryableFailure;
  Executor::configure(f.host());
  assert(Executor::ensureRunning());
  fake::completeInSend = true;
  const Key key(4, 5, 6);
  assert(Executor::enqueueTile(key));
  assert(f.tiles == 1 && Executor::pending() == 0 && !Executor::seen(key));
  f.outcome = Executor::Outcome::Success;
  assert(Executor::enqueueTile(key) && f.tiles == 2 && Executor::seen(key));
  assert(!Executor::enqueueTile(key) && f.tiles == 2);
  Executor::forget(key);
  assert(!Executor::seen(key));
  assert(Executor::enqueueTile(key) && f.tiles == 3 && Executor::pending() == 0);
}
void activeForget() {
  Fixture f;
  Executor::configure(f.host());
  const Key key(3, 9, 12);
  assert(Executor::enqueueTile(key) && Executor::pending() == 1);
  Executor::forget(key);
  assert(Executor::seen(key)); // Active request cannot be forgotten mid-flight.
  Executor::clearRecent();
  assert(Executor::seen(key) && !Executor::enqueueTile(key));
  fake::pump();
  assert(Executor::pending() == 0 && !Executor::seen(key));
  assert(Executor::enqueueTile(key));
  fake::pump();
  assert(f.tiles == 2 && Executor::pending() == 0);
}
void priorityAndPacing() {
  Fixture f;
  f.prioritiesRemaining = 2;
  Executor::configure(f.host());
  assert(Executor::enqueueTile(Key(1, 2, 3)));
  fake::events.clear();
  fake::pump();
  assert(f.tiles == 1 && Executor::pending() == 0);
  assert(fake::events == "P,P,P,R250,D1,T,D7,P,R250");
  assert(!fake::leaseActive && fake::httpClosed);
  const auto d = Executor::diagnostics();
  assert(d.iterations == 1 && d.step == 'q' && d.spawnOk);
}
void telemetry() {
  Executor::noteStep('s');
  Executor::noteHttpCode(404);
  Executor::noteWrite('w');
  Executor::noteOpenFailure();
  Executor::noteShortWrite();
  Executor::noteSuccess();
  Executor::noteFailure();
  const auto d = Executor::diagnostics();
  assert(d.step == 's' && d.lastHttpCode == 404 && d.lastWrite == 'w');
  assert(d.openFailures == 1 && d.shortWrites == 1 && d.ok == 1 && d.failed == 1);
}
void deferredAdmission() {
  Fixture f;
  f.deferrals = 2;
  Executor::configure(f.host());
  const Key key(2, 7, 9);
  assert(Executor::enqueueTile(key));
  fake::events.clear();
  fake::pump();
  assert(f.tiles == 1 && Executor::pending() == 0 && Executor::seen(key));
  assert(Executor::diagnostics().iterations == 1);
  assert(fake::events == "P,R250,D1,W,D1,P,P,D1,W,D1,P,P,D1,T,D7,P,R250");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const std::string scenario(argv[1]);
  if (scenario == "queuecreatefail") queueCreateFailure();
  else if (scenario == "stackallocfail") stackFailure();
  else if (scenario == "taskcreatefail") taskFailureRetry();
  else if (scenario == "fullqueue") fullQueueRollback();
  else if (scenario == "immediate") immediateCompletion();
  else if (scenario == "activeforget") activeForget();
  else if (scenario == "priority") priorityAndPacing();
  else if (scenario == "diagnostics") telemetry();
  else if (scenario == "deferred") deferredAdmission();
  else return 2;
  std::cout << "network executor " << scenario << " passed\n";
  return 0;
}
