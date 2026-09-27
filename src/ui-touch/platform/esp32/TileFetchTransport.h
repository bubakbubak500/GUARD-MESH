// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SharedNetworkExecutor.h"
#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace ui { namespace platform {

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
// Borrowed only while Host::withBackendLease is running. The prefix and URL
// base are copied so a request cannot mix two map styles or preference values.
struct TileFetchBackend {
  fs::FS *filesystem = nullptr;
  char prefix[16] = {};
  char server[80] = {};
  uint8_t style = 0;
  bool ownLittleFs = false;
  bool markStorageIo = false;
  bool cardBackend = false;
};

struct TileFetchResult {
  SharedNetworkExecutor::Completion completion;
  bool publish = false;
  bool cacheHit = false;
  uint8_t zoom = 0;
};

struct TileFetchHost {
  void *context = nullptr;
  TileFetchResult (*withBackendLease)(void *, TileFetchResult (*)(void *), void *) = nullptr;
  bool (*captureBackend)(void *, TileFetchBackend *) = nullptr;
  bool (*lowSpace)(void *) = nullptr;
  void (*noteWritten)(void *, size_t) = nullptr;
  void (*markSdIo)(void *) = nullptr;
  // Read immediately before opening the output File. On Pager this observes
  // a card-removal latch that may have changed since captureBackend.
  bool (*lateCardAllowed)(void *, bool cardBackend) = nullptr;
  void (*cardWriteFailed)(void *) = nullptr;
  // Called after socket close and backend-lease release. The host decides
  // whether this zoom is visible; cache hits repaint only on ThinkNode M9.
  void (*publishVisible)(void *, uint8_t zoom, bool cacheHit) = nullptr;
};

class TileFetchTransport {
public:
  static SharedNetworkExecutor::Completion run(
      const SharedNetworkExecutor::TileWork &, void *client, void *http,
      const TileFetchHost &);
};
#endif

} } // namespace ui::platform
