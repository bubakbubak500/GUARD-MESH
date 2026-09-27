// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../services/TileRequestLedger.h"
#include <cstddef>
#include <cstdint>

namespace ui { namespace platform {

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
// Process-lifetime core-0 executor. The UI thread alone configures, reserves,
// starts and enqueues. It is intentionally permanent: there is no stop/join
// contract while HTTP and firmware-install callbacks can block.
class SharedNetworkExecutor {
public:
  using TileKey = TileRequestKey;
  struct TileWork { TileKey key; uint32_t token; };
  // Deferred means admission was closed before any work began. Retain this
  // request/token and service priority jobs before retrying; it is not a failure.
  enum class Outcome : uint8_t { Success, RetryableFailure, PermanentFailure, Deferred };
  struct Completion {
    Outcome outcome;
    uint16_t pacingMs; // sleep after completion and backend lease release
    Completion(Outcome result = Outcome::RetryableFailure, uint16_t delayMs = 0)
      : outcome(result), pacingMs(delayMs) {}
  };
  struct Host {
    void *context = nullptr;
    bool (*runPriority)(void *, void *client, void *http) = nullptr;
    Completion (*runTile)(void *, const TileWork &, void *client, void *http) = nullptr;
  };
  struct Diagnostics {
    uint16_t iterations = 0, ok = 0, failed = 0;
    uint16_t openFailures = 0, shortWrites = 0;
    int16_t lastHttpCode = 0;
    char step = '-', lastWrite = '-';
    bool spawnOk = false;
  };

  static void configure(Host);          // UI thread, before first start
  static bool reserveEarly();           // UI thread, before Wi-Fi consumes internal heap
  static bool ensureRunning();          // UI thread; retains queue if stack creation fails
  static bool enqueueTile(const TileKey &); // UI thread; dedup + pending reservation
  static bool seen(const TileKey &);    // UI thread
  static void forget(const TileKey &);  // UI thread, cache invalidation
  static void clearRecent();            // UI thread, does not alter active pending
  static uint16_t pending();            // any thread
  static bool started();                // task created; not a liveness probe
  static size_t stackBytes();
  static Diagnostics diagnostics();

  // Worker-side tile telemetry; relaxed atomics make UI snapshots race-free.
  static void noteStep(char);
  static void noteHttpCode(int16_t);
  static void noteWrite(char);
  static void noteOpenFailure();
  static void noteShortWrite();
  static void noteSuccess();
  static void noteFailure();
};
#endif

} } // namespace ui::platform
