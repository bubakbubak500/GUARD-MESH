// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/Sightline.h"
#include <atomic>
#include <stdint.h>
namespace ui {
// One UI producer/consumer and one worker; owner must outlive the worker.
// A completed result must be consumed before another request can be published.
class SightlineJob {
public:
  struct Request {
    uint32_t id;
    sightline::Path path;
    char server[80];
  };
  struct Result {
    Request request;
    float elevations[sightline::Samples];
    int code, parsed, valid;
    bool ok;
  };
  struct Backend {
    void *context;
    int (*fetch)(void *, const Request &, float *, int &code);
    void (*retryDelay)(void *);
  };
  bool request(const Request &); // UI only
  bool take(Result &);           // UI only
  bool active() const;
  int attempt() const { return _attempt.load(std::memory_order_relaxed); }
  bool run(const Backend &); // worker only; callbacks synchronous
private:
  enum State { Idle, Queued, Running, Ready };
  std::atomic<State> _state{Idle};
  std::atomic<int> _attempt{0};
  Result _result{};
};
} // namespace ui
