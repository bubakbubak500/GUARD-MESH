// SPDX-License-Identifier: GPL-3.0-or-later
#include "SharedNetworkExecutor.h"
#include "../../device_caps.h"

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#if defined(HAS_TDISPLAY_P4)
#include <C6Socket.h>
#include <C6WifiShim.h>
#define WiFiClient C6Client
#endif

namespace ui { namespace platform {
namespace {
using Executor = SharedNetworkExecutor;
using TileWork = Executor::TileWork;

// There is one process-lifetime executor. The task, queue, TCB and stack are
// deliberately never deleted: the static task cannot be safely respawned while
// the FreeRTOS idle reaper may still reference its TCB, and transports have no
// bounded cooperative cancellation protocol today.
Executor::Host s_host{};
QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
StackType_t *s_stack = nullptr;
size_t s_stackBytes = 0;
StaticTask_t s_tcb;
portMUX_TYPE s_ledgerMux = portMUX_INITIALIZER_UNLOCKED;
TileRequestLedger s_ledger;

std::atomic<uint16_t> s_iterations{0}, s_ok{0}, s_failed{0};
std::atomic<uint16_t> s_openFailures{0}, s_shortWrites{0};
std::atomic<int16_t> s_lastHttpCode{0};
std::atomic<char> s_step{'-'}, s_lastWrite{'-'};
std::atomic<bool> s_spawnOk{false};

void taskMain(void *) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  s_step.store('x', std::memory_order_relaxed);
  TileWork work{};
  bool retained = false;
  for (;;) {
    if (s_host.runPriority && s_host.runPriority(s_host.context, &client, &http))
      continue;
    s_step.store('q', std::memory_order_relaxed);
    if (!retained && xQueueReceive(s_queue, &work, pdMS_TO_TICKS(250)) != pdTRUE)
      continue;
    // Cache-hit bursts must still yield to the core-0 idle task.
    vTaskDelay(1);
    const Executor::Completion result = s_host.runTile
      ? s_host.runTile(s_host.context, work, &client, &http)
      : Executor::Completion{};
    if (result.outcome == Executor::Outcome::Deferred) {
      retained = true;
      // Do not complete the ledger entry or dequeue another tile. In
      // particular, never park here waiting for a backend pause: the next
      // iteration must let the same executor drain its priority jobs.
      vTaskDelay(result.pacingMs ? pdMS_TO_TICKS(result.pacingMs) : 1);
      continue;
    }
    retained = false;
    s_iterations.fetch_add(1, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_ledgerMux);
    s_ledger.complete(work.token, result.outcome == Executor::Outcome::RetryableFailure);
    portEXIT_CRITICAL(&s_ledgerMux);
    // Host.runTile must release its tile-backend lease before returning. This
    // delay is network pacing, never part of the SD VFS busy window.
    if (result.pacingMs) vTaskDelay(pdMS_TO_TICKS(result.pacingMs));
  }
}
} // namespace

void SharedNetworkExecutor::configure(Host host) {
  // UI-thread setup only. Once started, the worker may read the Host on core 0.
  if (!s_task) s_host = host;
}
bool SharedNetworkExecutor::reserveEarly() {
  if (s_stack) return true;
  const size_t sizes[] = {8 * 1024, 7 * 1024, 6 * 1024};
  for (size_t bytes : sizes) {
    s_stack = static_cast<StackType_t *>(
      heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (s_stack) { s_stackBytes = bytes; break; }
  }
  return s_stack != nullptr;
}
bool SharedNetworkExecutor::ensureRunning() {
  // UI thread alone creates the queue/task; this is not a concurrent ensure.
  if (!s_host.runTile) return false; // never start a consumer that drops tiles
  if (!s_queue) {
    s_queue = xQueueCreate(TileRequestLedger::QueueCapacity, sizeof(TileWork));
    if (!s_queue) return false;
  }
  if (s_task) return true;
  if (!reserveEarly()) { s_spawnOk.store(false, std::memory_order_relaxed); return false; }
  s_step.store('-', std::memory_order_relaxed);
  s_spawnOk.store(false, std::memory_order_relaxed);
  s_task = xTaskCreateStaticPinnedToCore(taskMain, "tile_fetch",
    s_stackBytes / sizeof(StackType_t), nullptr, 1, s_stack, &s_tcb, 0);
  s_spawnOk.store(s_task != nullptr, std::memory_order_relaxed);
  return s_task != nullptr;
}
bool SharedNetworkExecutor::enqueueTile(const TileKey &key) {
  if (!ensureRunning()) return false;
  portENTER_CRITICAL(&s_ledgerMux);
  const uint32_t token = s_ledger.reserve(key);
  portEXIT_CRITICAL(&s_ledgerMux);
  if (!token) return false;
  TileWork work{};
  work.key = key;
  work.token = token;
  // Never call FreeRTOS queue APIs under the ledger lock. The worker may
  // consume and complete immediately after this publication.
  if (xQueueSend(s_queue, &work, 0) == pdTRUE) return true;
  portENTER_CRITICAL(&s_ledgerMux);
  s_ledger.rollback(token);
  portEXIT_CRITICAL(&s_ledgerMux);
  return false;
}
bool SharedNetworkExecutor::seen(const TileKey &key) {
  portENTER_CRITICAL(&s_ledgerMux);
  const bool result = s_ledger.seen(key);
  portEXIT_CRITICAL(&s_ledgerMux);
  return result;
}
void SharedNetworkExecutor::forget(const TileKey &key) {
  portENTER_CRITICAL(&s_ledgerMux);
  s_ledger.forget(key);
  portEXIT_CRITICAL(&s_ledgerMux);
}
void SharedNetworkExecutor::clearRecent() {
  portENTER_CRITICAL(&s_ledgerMux);
  s_ledger.clearRecent();
  portEXIT_CRITICAL(&s_ledgerMux);
}
uint16_t SharedNetworkExecutor::pending() {
  portENTER_CRITICAL(&s_ledgerMux);
  const uint16_t result = s_ledger.pending();
  portEXIT_CRITICAL(&s_ledgerMux);
  return result;
}
bool SharedNetworkExecutor::started() { return s_task != nullptr; }
size_t SharedNetworkExecutor::stackBytes() { return s_stackBytes; }
SharedNetworkExecutor::Diagnostics SharedNetworkExecutor::diagnostics() {
  Diagnostics value{};
  value.iterations = s_iterations.load(std::memory_order_relaxed);
  value.ok = s_ok.load(std::memory_order_relaxed);
  value.failed = s_failed.load(std::memory_order_relaxed);
  value.openFailures = s_openFailures.load(std::memory_order_relaxed);
  value.shortWrites = s_shortWrites.load(std::memory_order_relaxed);
  value.lastHttpCode = s_lastHttpCode.load(std::memory_order_relaxed);
  value.step = s_step.load(std::memory_order_relaxed);
  value.lastWrite = s_lastWrite.load(std::memory_order_relaxed);
  value.spawnOk = s_spawnOk.load(std::memory_order_relaxed);
  return value;
}
void SharedNetworkExecutor::noteStep(char value) { s_step.store(value, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteHttpCode(int16_t value) { s_lastHttpCode.store(value, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteWrite(char value) { s_lastWrite.store(value, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteOpenFailure() { s_openFailures.fetch_add(1, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteShortWrite() { s_shortWrites.fetch_add(1, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteSuccess() { s_ok.fetch_add(1, std::memory_order_relaxed); }
void SharedNetworkExecutor::noteFailure() { s_failed.fetch_add(1, std::memory_order_relaxed); }

} } // namespace ui::platform
#endif
